#include "LogicalUnitMetafile.h"
#include "MetafileToRenderer.h"
#include "MetafileToRendererReader.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace NSOnlineOfficeBinToPdf
{
	namespace
	{
		void AppendU32(std::vector<unsigned char>& bytes, std::uint32_t value)
		{
			bytes.push_back(static_cast<unsigned char>(value));
			bytes.push_back(static_cast<unsigned char>(value >> 8));
			bytes.push_back(static_cast<unsigned char>(value >> 16));
			bytes.push_back(static_cast<unsigned char>(value >> 24));
		}

		void WriteU32(std::vector<unsigned char>& bytes, std::size_t offset, std::uint32_t value)
		{
			bytes[offset] = static_cast<unsigned char>(value);
			bytes[offset + 1] = static_cast<unsigned char>(value >> 8);
			bytes[offset + 2] = static_cast<unsigned char>(value >> 16);
			bytes[offset + 3] = static_cast<unsigned char>(value >> 24);
		}

		CRendererLogicalUnit MakeUnit()
		{
			CRendererLogicalUnit unit;
			unit.Unicode = {0x41u, 0x1F600u, 0x0301u};
			unit.LogicalAdvance = 1.23456;
			unit.VisualX = -12.5;
			unit.VisualY = 7.25;
			unit.Components = {{40, -0.125, 0.5}, {57, 0.75, -1.25}, {62, 1.125, 2.0}};
			return unit;
		}

		std::vector<unsigned char> Payload(const std::vector<unsigned char>& record)
		{
			return std::vector<unsigned char>(record.begin() + 4, record.end());
		}

		ELogicalUnitRecordResult Parse(const std::vector<unsigned char>& payload,
		                               CRendererLogicalUnit& unit,
		                               CLogicalUnitRecordError* error = nullptr)
		{
			return ParseLogicalUnitRecord(payload.data(), payload.size(), unit, error);
		}
	}

	TEST(LogicalUnitMetafile, RoundTripsEveryVersionOneField)
	{
		const CRendererLogicalUnit input = MakeUnit();
		std::vector<unsigned char> record;
		CLogicalUnitRecordError error;
		ASSERT_TRUE(SerializeLogicalUnitRecord(input, record, &error)) << error.Message;
		ASSERT_EQ(76u, record.size());

		CRendererLogicalUnit output;
		ASSERT_EQ(ELogicalUnitRecordResult::Parsed, Parse(Payload(record), output, &error))
			<< error.Message;
		EXPECT_EQ(input.Unicode, output.Unicode);
		EXPECT_DOUBLE_EQ(input.LogicalAdvance, output.LogicalAdvance);
		EXPECT_DOUBLE_EQ(input.VisualX, output.VisualX);
		EXPECT_DOUBLE_EQ(input.VisualY, output.VisualY);
		ASSERT_EQ(input.Components.size(), output.Components.size());
		for (std::size_t index = 0; index < input.Components.size(); ++index)
		{
			EXPECT_EQ(input.Components[index].SourceGid, output.Components[index].SourceGid);
			EXPECT_DOUBLE_EQ(input.Components[index].RelativeX, output.Components[index].RelativeX);
			EXPECT_DOUBLE_EQ(input.Components[index].RelativeY, output.Components[index].RelativeY);
		}
	}

	TEST(LogicalUnitMetafile, UsesLittleEndianLengthAndFixedPointQuantization)
	{
		CRendererLogicalUnit input = MakeUnit();
		input.LogicalAdvance = 1.234569;
		input.VisualX = -1.234569;
		std::vector<unsigned char> record;
		ASSERT_TRUE(SerializeLogicalUnitRecord(input, record));
		EXPECT_EQ(record.size(), static_cast<std::size_t>(record[0] | record[1] << 8));

		CRendererLogicalUnit output;
		ASSERT_EQ(ELogicalUnitRecordResult::Parsed, Parse(Payload(record), output));
		EXPECT_DOUBLE_EQ(1.23456, output.LogicalAdvance);
		EXPECT_DOUBLE_EQ(-1.23456, output.VisualX);
	}

	TEST(LogicalUnitMetafile, PreservesMultipleUnitAndComponentOrder)
	{
		CRendererLogicalUnit first = MakeUnit();
		CRendererLogicalUnit second = MakeUnit();
		second.Unicode = {0x42u};
		second.Components = {{9, 2.0, 3.0}, {8, 1.0, 4.0}};
		std::vector<unsigned char> firstRecord;
		std::vector<unsigned char> secondRecord;
		ASSERT_TRUE(SerializeLogicalUnitRecord(first, firstRecord));
		ASSERT_TRUE(SerializeLogicalUnitRecord(second, secondRecord));

		std::vector<unsigned char> stream;
		stream.push_back(ctDrawTextLogicalUnit);
		stream.insert(stream.end(), firstRecord.begin(), firstRecord.end());
		stream.push_back(ctDrawTextLogicalUnit);
		stream.insert(stream.end(), secondRecord.begin(), secondRecord.end());
		CBufferReader reader(stream.data(), static_cast<int>(stream.size()));
		std::vector<CRendererLogicalUnit> parsed;
		while (reader.Check())
		{
			ASSERT_EQ(ctDrawTextLogicalUnit, reader.ReadByte());
			const unsigned char* payload = nullptr;
			std::size_t payloadSize = 0;
			ASSERT_TRUE(reader.TryReadBoundedRecord(MaximumLogicalUnitRecordBytes, payload, payloadSize));
			CRendererLogicalUnit unit;
			ASSERT_EQ(ELogicalUnitRecordResult::Parsed,
			          ParseLogicalUnitRecord(payload, payloadSize, unit));
			parsed.push_back(std::move(unit));
		}
		ASSERT_EQ(2u, parsed.size());
		EXPECT_EQ(0x41u, parsed[0].Unicode[0]);
		EXPECT_EQ(0x42u, parsed[1].Unicode[0]);
		ASSERT_EQ(2u, parsed[1].Components.size());
		EXPECT_EQ(9u, parsed[1].Components[0].SourceGid);
		EXPECT_EQ(8u, parsed[1].Components[1].SourceGid);
	}

	TEST(LogicalUnitMetafile, UnknownVersionIsSkippedByBoundedFraming)
	{
		std::vector<unsigned char> record;
		ASSERT_TRUE(SerializeLogicalUnitRecord(MakeUnit(), record));
		record[4] = 2;
		record.push_back(ctDrawTextCodeGid);
		CBufferReader reader(record.data(), static_cast<int>(record.size()));
		const unsigned char* payload = nullptr;
		std::size_t payloadSize = 0;
		ASSERT_TRUE(reader.TryReadBoundedRecord(MaximumLogicalUnitRecordBytes, payload, payloadSize));
		CRendererLogicalUnit unchanged = MakeUnit();
		EXPECT_EQ(ELogicalUnitRecordResult::UnsupportedVersion,
		          ParseLogicalUnitRecord(payload, payloadSize, unchanged));
		EXPECT_EQ((std::vector<unsigned int>{0x41u, 0x1F600u, 0x0301u}), unchanged.Unicode);
		ASSERT_EQ(1u, reader.Remaining());
		EXPECT_EQ(ctDrawTextCodeGid, reader.ReadByte());
	}

	TEST(LogicalUnitMetafile, SkipsMinimalUnknownVersionRecord)
	{
		const std::vector<unsigned char> payload = {2};
		CRendererLogicalUnit unchanged = MakeUnit();
		EXPECT_EQ(ELogicalUnitRecordResult::UnsupportedVersion, Parse(payload, unchanged));
		EXPECT_EQ((std::vector<unsigned int>{0x41u, 0x1F600u, 0x0301u}), unchanged.Unicode);
		EXPECT_EQ(3u, unchanged.Components.size());
	}

	TEST(LogicalUnitMetafile, RejectsInvalidOuterLengthsWithoutAdvancing)
	{
		for (std::uint32_t length : {0u, 3u, 0xFFFFFFFFu})
		{
			std::vector<unsigned char> bytes;
			AppendU32(bytes, length);
			CBufferReader reader(bytes.data(), static_cast<int>(bytes.size()));
			const unsigned char* payload = nullptr;
			std::size_t payloadSize = 0;
			EXPECT_FALSE(reader.TryReadBoundedRecord(MaximumLogicalUnitRecordBytes,
			                                         payload, payloadSize));
			EXPECT_EQ(4u, reader.Remaining());
		}
	}

	TEST(LogicalUnitMetafile, RejectsTruncatedOuterRecordWithoutAdvancing)
	{
		std::vector<unsigned char> bytes;
		AppendU32(bytes, 100);
		bytes.resize(20, 0);
		CBufferReader reader(bytes.data(), static_cast<int>(bytes.size()));
		const unsigned char* payload = nullptr;
		std::size_t payloadSize = 0;
		EXPECT_FALSE(reader.TryReadBoundedRecord(MaximumLogicalUnitRecordBytes,
		                                         payload, payloadSize));
		EXPECT_EQ(bytes.size(), reader.Remaining());
	}

	TEST(LogicalUnitMetafile, RejectsZeroAndExcessiveUnicodeCounts)
	{
		std::vector<unsigned char> record;
		ASSERT_TRUE(SerializeLogicalUnitRecord(MakeUnit(), record));
		for (std::uint32_t count : {0u, 0xFFFFFFFFu,
		                            static_cast<std::uint32_t>(MaximumLogicalUnitUnicodeScalars + 1)})
		{
			std::vector<unsigned char> payload = Payload(record);
			WriteU32(payload, 4, count);
			CRendererLogicalUnit output;
			EXPECT_EQ(ELogicalUnitRecordResult::Malformed, Parse(payload, output));
		}
	}

	TEST(LogicalUnitMetafile, RejectsSurrogatesAndOutOfRangeScalars)
	{
		std::vector<unsigned char> record;
		ASSERT_TRUE(SerializeLogicalUnitRecord(MakeUnit(), record));
		for (std::uint32_t scalar : {0xD800u, 0xDFFFu, 0x110000u})
		{
			std::vector<unsigned char> payload = Payload(record);
			WriteU32(payload, 8, scalar);
			CRendererLogicalUnit output;
			EXPECT_EQ(ELogicalUnitRecordResult::Malformed, Parse(payload, output));
		}
	}

	TEST(LogicalUnitMetafile, RejectsZeroAndExcessiveComponentCounts)
	{
		CRendererLogicalUnit input;
		input.Unicode = {0x41u};
		input.LogicalAdvance = 1.0;
		input.Components = {{1, 0.0, 0.0}};
		std::vector<unsigned char> record;
		ASSERT_TRUE(SerializeLogicalUnitRecord(input, record));
		for (std::uint32_t count : {0u, 0xFFFFFFFFu,
		                            static_cast<std::uint32_t>(MaximumLogicalUnitComponents + 1)})
		{
			std::vector<unsigned char> payload = Payload(record);
			WriteU32(payload, 24, count);
			CRendererLogicalUnit output;
			EXPECT_EQ(ELogicalUnitRecordResult::Malformed, Parse(payload, output));
		}
	}

	TEST(LogicalUnitMetafile, AcceptsProtocolCountLimitsAndGidBounds)
	{
		CRendererLogicalUnit input;
		input.Unicode.assign(MaximumLogicalUnitUnicodeScalars, 0x41u);
		input.LogicalAdvance = 1.0;
		input.Components.assign(MaximumLogicalUnitComponents, {1, 0.0, 0.0});
		input.Components.back().SourceGid = 65535;
		std::vector<unsigned char> record;
		ASSERT_TRUE(SerializeLogicalUnitRecord(input, record));
		ASSERT_LE(record.size(), MaximumLogicalUnitRecordBytes);
		CRendererLogicalUnit output;
		ASSERT_EQ(ELogicalUnitRecordResult::Parsed, Parse(Payload(record), output));
		EXPECT_EQ(MaximumLogicalUnitUnicodeScalars, output.Unicode.size());
		EXPECT_EQ(MaximumLogicalUnitComponents, output.Components.size());
		EXPECT_EQ(1u, output.Components.front().SourceGid);
		EXPECT_EQ(65535u, output.Components.back().SourceGid);
	}

	TEST(LogicalUnitMetafile, RejectsInvalidGidsAndTruncatedComponents)
	{
		CRendererLogicalUnit input;
		input.Unicode = {0x41u};
		input.LogicalAdvance = 1.0;
		input.Components = {{1, 0.0, 0.0}};
		std::vector<unsigned char> record;
		ASSERT_TRUE(SerializeLogicalUnitRecord(input, record));
		for (std::uint32_t gid : {0u, 65536u, 0xFFFFFFFFu})
		{
			std::vector<unsigned char> payload = Payload(record);
			WriteU32(payload, 28, gid);
			CRendererLogicalUnit output;
			EXPECT_EQ(ELogicalUnitRecordResult::Malformed, Parse(payload, output));
		}
		std::vector<unsigned char> truncated = Payload(record);
		truncated.pop_back();
		CRendererLogicalUnit output;
		EXPECT_EQ(ELogicalUnitRecordResult::Malformed, Parse(truncated, output));
	}

	TEST(LogicalUnitMetafile, RejectsTruncatedVersionOneHeaderAndGeometry)
	{
		for (std::size_t size = 0; size < 4; ++size)
		{
			std::vector<unsigned char> payload(size, 0);
			if (!payload.empty())
				payload[0] = LogicalUnitCommandVersion;
			CRendererLogicalUnit output;
			EXPECT_EQ(ELogicalUnitRecordResult::Malformed, Parse(payload, output));
		}

		std::vector<unsigned char> record;
		ASSERT_TRUE(SerializeLogicalUnitRecord(MakeUnit(), record));
		std::vector<unsigned char> truncated = Payload(record);
		truncated.resize(31);
		CRendererLogicalUnit output;
		EXPECT_EQ(ELogicalUnitRecordResult::Malformed, Parse(truncated, output));
	}

	TEST(LogicalUnitMetafile, RejectsNegativeAdvanceWithoutChangingOutput)
	{
		std::vector<unsigned char> record;
		ASSERT_TRUE(SerializeLogicalUnitRecord(MakeUnit(), record));
		std::vector<unsigned char> payload = Payload(record);
		WriteU32(payload, 20, 0xFFFFFFFFu);
		CRendererLogicalUnit output = MakeUnit();
		EXPECT_EQ(ELogicalUnitRecordResult::Malformed, Parse(payload, output));
		EXPECT_EQ((std::vector<unsigned int>{0x41u, 0x1F600u, 0x0301u}), output.Unicode);
		EXPECT_DOUBLE_EQ(1.23456, output.LogicalAdvance);
		EXPECT_EQ(3u, output.Components.size());
	}

	TEST(LogicalUnitMetafile, RejectsUnknownFlagsReservedAndTrailingBytes)
	{
		std::vector<unsigned char> record;
		ASSERT_TRUE(SerializeLogicalUnitRecord(MakeUnit(), record));
		for (std::size_t offset : {1u, 2u, 3u})
		{
			std::vector<unsigned char> payload = Payload(record);
			payload[offset] = 1;
			CRendererLogicalUnit output;
			EXPECT_EQ(ELogicalUnitRecordResult::Malformed, Parse(payload, output));
		}
		std::vector<unsigned char> trailing = Payload(record);
		trailing.push_back(0);
		CRendererLogicalUnit output;
		EXPECT_EQ(ELogicalUnitRecordResult::Malformed, Parse(trailing, output));
	}

	TEST(LogicalUnitMetafile, KeepsLegacyCommandIdAndIntroducesSeparateLogicalCommand)
	{
		EXPECT_EQ(83, ctDrawTextCodeGid);
		EXPECT_EQ(84, ctDrawTextLogicalUnit);
		const std::vector<unsigned char> legacyHeader = {
			ctDrawTextCodeGid, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
			0x41, 0, 0, 0};
		EXPECT_EQ(21u, legacyHeader.size());
		EXPECT_EQ(ctDrawTextCodeGid, legacyHeader.front());
	}

	TEST(LogicalUnitMetafile, RejectsInvalidSerializerInputWithoutChangingOutput)
	{
		std::vector<unsigned char> unchanged = {1, 2, 3};
		CLogicalUnitRecordError error;
		CRendererLogicalUnit input = MakeUnit();
		input.Unicode.clear();
		EXPECT_FALSE(SerializeLogicalUnitRecord(input, unchanged, &error));
		EXPECT_EQ((std::vector<unsigned char>{1, 2, 3}), unchanged);

		input = MakeUnit();
		input.VisualX = std::numeric_limits<double>::infinity();
		EXPECT_FALSE(SerializeLogicalUnitRecord(input, unchanged, &error));
		EXPECT_EQ((std::vector<unsigned char>{1, 2, 3}), unchanged);
	}
}
