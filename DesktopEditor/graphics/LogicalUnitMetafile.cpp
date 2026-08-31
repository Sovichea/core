/*
 * (c) Copyright Ascensio System SIA 2010-2023
 *
 * This program is distributed under the GNU Affero General Public License
 * version 3. See the LICENSE file for details.
 */
#include "LogicalUnitMetafile.h"

#include <cmath>
#include <limits>

namespace NSOnlineOfficeBinToPdf
{
	namespace
	{
		constexpr double FixedScale = 100000.0;
		constexpr std::size_t FixedPayloadBytes = 24;

		bool SetError(CLogicalUnitRecordError* error, std::size_t offset, const char* message)
		{
			if (error != nullptr)
			{
				error->Offset = offset;
				error->Message = message;
			}
			return false;
		}

		bool IsUnicodeScalar(std::uint32_t value)
		{
			return value <= 0x10FFFFu && !(value >= 0xD800u && value <= 0xDFFFu);
		}

		class CRecordReader
		{
		public:
			CRecordReader(const unsigned char* data, std::size_t size)
				: m_data(data), m_size(size)
			{
			}

			std::size_t Offset() const { return m_offset; }
			std::size_t Remaining() const { return m_size - m_offset; }

			bool ReadU8(std::uint8_t& value)
			{
				if (Remaining() < 1)
					return false;
				value = m_data[m_offset++];
				return true;
			}

			bool ReadU16(std::uint16_t& value)
			{
				if (Remaining() < 2)
					return false;
				value = static_cast<std::uint16_t>(m_data[m_offset]) |
				        static_cast<std::uint16_t>(m_data[m_offset + 1]) << 8;
				m_offset += 2;
				return true;
			}

			bool ReadU32(std::uint32_t& value)
			{
				if (Remaining() < 4)
					return false;
				value = static_cast<std::uint32_t>(m_data[m_offset]) |
				        static_cast<std::uint32_t>(m_data[m_offset + 1]) << 8 |
				        static_cast<std::uint32_t>(m_data[m_offset + 2]) << 16 |
				        static_cast<std::uint32_t>(m_data[m_offset + 3]) << 24;
				m_offset += 4;
				return true;
			}

			bool ReadFixed(double& value)
			{
				std::uint32_t raw = 0;
				if (!ReadU32(raw))
					return false;
				value = static_cast<double>(static_cast<std::int32_t>(raw)) / FixedScale;
				return true;
			}

		private:
			const unsigned char* m_data = nullptr;
			std::size_t m_size = 0;
			std::size_t m_offset = 0;
		};

		void AppendU16(std::vector<unsigned char>& output, std::uint16_t value)
		{
			output.push_back(static_cast<unsigned char>(value));
			output.push_back(static_cast<unsigned char>(value >> 8));
		}

		void AppendU32(std::vector<unsigned char>& output, std::uint32_t value)
		{
			output.push_back(static_cast<unsigned char>(value));
			output.push_back(static_cast<unsigned char>(value >> 8));
			output.push_back(static_cast<unsigned char>(value >> 16));
			output.push_back(static_cast<unsigned char>(value >> 24));
		}

		bool TryFixed(double value, std::int32_t& fixed, CLogicalUnitRecordError* error)
		{
			if (!std::isfinite(value))
				return SetError(error, 0, "logical unit coordinate is not finite");
			const double scaled = std::trunc(value * FixedScale);
			if (!std::isfinite(scaled) || scaled < std::numeric_limits<std::int32_t>::min() ||
			    scaled > std::numeric_limits<std::int32_t>::max())
				return SetError(error, 0, "logical unit coordinate exceeds fixed-point range");
			fixed = static_cast<std::int32_t>(scaled);
			return true;
		}

		void AppendFixed(std::vector<unsigned char>& output, std::int32_t value)
		{
			AppendU32(output, static_cast<std::uint32_t>(value));
		}
	}

	ELogicalUnitRecordResult ParseLogicalUnitRecord(
		const unsigned char* payload,
		std::size_t payloadSize,
		CRendererLogicalUnit& unit,
		CLogicalUnitRecordError* error)
	{
		if (error != nullptr)
			*error = CLogicalUnitRecordError();
		if (payload == nullptr || payloadSize > MaximumLogicalUnitRecordBytes - 4 ||
		    payloadSize == 0)
		{
			SetError(error, 0, "logical unit record size is invalid");
			return ELogicalUnitRecordResult::Malformed;
		}

		CRecordReader reader(payload, payloadSize);
		std::uint8_t version = 0;
		if (!reader.ReadU8(version))
		{
			SetError(error, reader.Offset(), "logical unit version is truncated");
			return ELogicalUnitRecordResult::Malformed;
		}
		if (version != LogicalUnitCommandVersion)
			return ELogicalUnitRecordResult::UnsupportedVersion;
		if (payloadSize < FixedPayloadBytes)
		{
			SetError(error, reader.Offset(), "logical unit version 1 record is truncated");
			return ELogicalUnitRecordResult::Malformed;
		}

		std::uint8_t flags = 0;
		std::uint16_t reserved = 0;
		if (!reader.ReadU8(flags) || !reader.ReadU16(reserved))
		{
			SetError(error, reader.Offset(), "logical unit version header is truncated");
			return ELogicalUnitRecordResult::Malformed;
		}
		if (flags != 0 || reserved != 0)
		{
			SetError(error, 1, "logical unit version 1 flags and reserved field must be zero");
			return ELogicalUnitRecordResult::Malformed;
		}

		CRendererLogicalUnit parsed;
		std::uint32_t unicodeCount = 0;
		if (!reader.ReadU32(unicodeCount) || unicodeCount == 0 ||
		    unicodeCount > MaximumLogicalUnitUnicodeScalars ||
		    unicodeCount > reader.Remaining() / 4)
		{
			SetError(error, reader.Offset(), "logical unit Unicode count is invalid");
			return ELogicalUnitRecordResult::Malformed;
		}
		parsed.Unicode.reserve(unicodeCount);
		for (std::uint32_t index = 0; index < unicodeCount; ++index)
		{
			std::uint32_t scalar = 0;
			if (!reader.ReadU32(scalar) || !IsUnicodeScalar(scalar))
			{
				SetError(error, reader.Offset(), "logical unit contains an invalid Unicode scalar");
				return ELogicalUnitRecordResult::Malformed;
			}
			parsed.Unicode.push_back(scalar);
		}

		if (!reader.ReadFixed(parsed.LogicalAdvance) || !reader.ReadFixed(parsed.VisualX) ||
		    !reader.ReadFixed(parsed.VisualY) || parsed.LogicalAdvance < 0.0)
		{
			SetError(error, reader.Offset(), "logical unit geometry is truncated or invalid");
			return ELogicalUnitRecordResult::Malformed;
		}

		std::uint32_t componentCount = 0;
		if (!reader.ReadU32(componentCount) || componentCount == 0 ||
		    componentCount > MaximumLogicalUnitComponents ||
		    componentCount > reader.Remaining() / 12)
		{
			SetError(error, reader.Offset(), "logical unit component count is invalid");
			return ELogicalUnitRecordResult::Malformed;
		}
		parsed.Components.reserve(componentCount);
		for (std::uint32_t index = 0; index < componentCount; ++index)
		{
			CRendererLogicalComponent component;
			if (!reader.ReadU32(component.SourceGid) || component.SourceGid == 0 ||
			    component.SourceGid > 0xFFFFu || !reader.ReadFixed(component.RelativeX) ||
			    !reader.ReadFixed(component.RelativeY))
			{
				SetError(error, reader.Offset(), "logical unit component is truncated or invalid");
				return ELogicalUnitRecordResult::Malformed;
			}
			parsed.Components.push_back(component);
		}
		if (reader.Remaining() != 0)
		{
			SetError(error, reader.Offset(), "logical unit version 1 record has trailing bytes");
			return ELogicalUnitRecordResult::Malformed;
		}

		unit = std::move(parsed);
		return ELogicalUnitRecordResult::Parsed;
	}

	bool SerializeLogicalUnitRecord(
		const CRendererLogicalUnit& unit,
		std::vector<unsigned char>& record,
		CLogicalUnitRecordError* error)
	{
		if (error != nullptr)
			*error = CLogicalUnitRecordError();
		if (unit.Unicode.empty() || unit.Unicode.size() > MaximumLogicalUnitUnicodeScalars ||
		    unit.Components.empty() || unit.Components.size() > MaximumLogicalUnitComponents)
			return SetError(error, 0, "logical unit counts are invalid");
		for (std::uint32_t scalar : unit.Unicode)
		{
			if (!IsUnicodeScalar(scalar))
				return SetError(error, 0, "logical unit contains an invalid Unicode scalar");
		}

		std::int32_t logicalAdvance = 0;
		std::int32_t visualX = 0;
		std::int32_t visualY = 0;
		if (unit.LogicalAdvance < 0.0 || !TryFixed(unit.LogicalAdvance, logicalAdvance, error) ||
		    !TryFixed(unit.VisualX, visualX, error) || !TryFixed(unit.VisualY, visualY, error))
			return false;

		std::vector<std::int32_t> componentCoordinates;
		componentCoordinates.reserve(unit.Components.size() * 2);
		for (const CRendererLogicalComponent& component : unit.Components)
		{
			std::int32_t x = 0;
			std::int32_t y = 0;
			if (component.SourceGid == 0 || component.SourceGid > 0xFFFFu ||
			    !TryFixed(component.RelativeX, x, error) || !TryFixed(component.RelativeY, y, error))
				return SetError(error, 0, "logical unit component is invalid");
			componentCoordinates.push_back(x);
			componentCoordinates.push_back(y);
		}

		const std::size_t recordSize = 4 + FixedPayloadBytes + unit.Unicode.size() * 4 +
		                               unit.Components.size() * 12;
		if (recordSize > MaximumLogicalUnitRecordBytes ||
		    recordSize > std::numeric_limits<std::uint32_t>::max())
			return SetError(error, 0, "logical unit record is too large");

		std::vector<unsigned char> output;
		output.reserve(recordSize);
		AppendU32(output, static_cast<std::uint32_t>(recordSize));
		output.push_back(LogicalUnitCommandVersion);
		output.push_back(0);
		AppendU16(output, 0);
		AppendU32(output, static_cast<std::uint32_t>(unit.Unicode.size()));
		for (std::uint32_t scalar : unit.Unicode)
			AppendU32(output, scalar);
		AppendFixed(output, logicalAdvance);
		AppendFixed(output, visualX);
		AppendFixed(output, visualY);
		AppendU32(output, static_cast<std::uint32_t>(unit.Components.size()));
		for (std::size_t index = 0; index < unit.Components.size(); ++index)
		{
			AppendU32(output, unit.Components[index].SourceGid);
			AppendFixed(output, componentCoordinates[index * 2]);
			AppendFixed(output, componentCoordinates[index * 2 + 1]);
		}
		record = std::move(output);
		return true;
	}
}
