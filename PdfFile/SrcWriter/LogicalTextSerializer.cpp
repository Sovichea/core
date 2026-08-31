/*
 * (c) Copyright Ascensio System SIA 2010-2023
 *
 * This program is distributed under the GNU Affero General Public License
 * version 3. See the LICENSE file for details.
 */
#include "LogicalTextSerializer.h"

#include <cmath>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>

namespace PdfWriter
{
	namespace
	{
		constexpr std::size_t MaximumContentSize =
			static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
		constexpr std::size_t MaximumResourceNameLength = 127;
		constexpr double MaximumAbsoluteUserCoordinate = 1000000000000.0;

		bool SetError(CLogicalTextSerializationError& error,
		              CLogicalTextSerializationErrorCode code,
		              const std::string& message,
		              std::size_t commandIndex = CLogicalTextSerializationError::NoCommand)
		{
			error.Code = code;
			error.CommandIndex = commandIndex;
			error.Message = message;
			return false;
		}

		bool IsValidResourceName(const std::string& name)
		{
			if (name.empty() || name.size() > MaximumResourceNameLength)
				return false;
			for (unsigned char value : name)
			{
				const bool valid = (value >= 'A' && value <= 'Z') ||
				                   (value >= 'a' && value <= 'z') ||
				                   (value >= '0' && value <= '9') || value == '_' || value == '-';
				if (!valid)
					return false;
			}
			return true;
		}

		double RoundReal(double value, int precision)
		{
			double scale = 1.0;
			for (int index = 0; index < precision; ++index)
				scale *= 10.0;
			return std::round(value * scale) / scale;
		}

		std::string FormatReal(double value, int precision)
		{
			if (value == 0.0)
				value = 0.0;
			std::ostringstream stream;
			stream.imbue(std::locale::classic());
			stream << std::fixed << std::setprecision(precision) << value;
			std::string result = stream.str();
			const std::size_t decimal = result.find('.');
			if (decimal != std::string::npos)
			{
				while (!result.empty() && result.back() == '0')
					result.pop_back();
				if (!result.empty() && result.back() == '.')
					result.pop_back();
			}
			return result == "-0" ? "0" : result;
		}

		std::string FormatCid(TLogicalCid cid)
		{
			std::ostringstream stream;
			stream.imbue(std::locale::classic());
			stream << std::uppercase << std::hex << std::setfill('0') << std::setw(4) << cid;
			return stream.str();
		}

		struct CValidatedCommand
		{
			const CLogicalTextCommand* Command = nullptr;
			const CLogicalTextFontResource* Resource = nullptr;
			double PdfAdvance = 0.0;
		};

		bool TryValidateCommand(const CLogicalTextCommand& command,
		                        const std::vector<CLogicalTextFontResource>& resources,
		                        std::size_t index,
		                        CValidatedCommand& validated,
		                        CLogicalTextSerializationError& error)
		{
			if (command.Plan.Text.empty() || command.Plan.Visual.Components.empty() ||
			    !std::isfinite(command.Plan.VisualX) || !std::isfinite(command.Plan.VisualY))
				return SetError(error, CLogicalTextSerializationErrorCode::InvalidCommand,
				                "logical text command has empty content or non-finite placement", index);

			const TLogicalCid cid = command.Mapping.FontMapping.Cid;
			if (cid == 0 || cid > std::numeric_limits<std::uint16_t>::max() ||
			    command.Mapping.ShardIndex >= resources.size())
				return SetError(error, CLogicalTextSerializationErrorCode::InvalidMapping,
				                "logical text command has an invalid shard or CID", index);

			const CLogicalTextFontResource& resource = resources[command.Mapping.ShardIndex];
			if (!IsValidResourceName(resource.Name) || resource.Font == nullptr)
				return SetError(error, CLogicalTextSerializationErrorCode::InvalidResource,
				                "logical text command references an invalid font resource", index);
			if (cid >= resource.Font->Widths.size())
				return SetError(error, CLogicalTextSerializationErrorCode::InvalidWidth,
				                "logical text CID has no serialized PDF width", index);

			const int width = resource.Font->Widths[cid];
			if (width < 0)
				return SetError(error, CLogicalTextSerializationErrorCode::InvalidWidth,
				                "logical text CID has a negative serialized PDF width", index);

			validated.Command = &command;
			validated.Resource = &resource;
			validated.PdfAdvance = static_cast<double>(width) / 1000.0;
			return true;
		}

		bool CanJoin(const CValidatedCommand& current,
		             const CValidatedCommand& next,
		             double groupBaseline,
		             double reconstructedCurrentX,
		             const CLogicalTextSerializationOptions& options,
		             double& adjustment,
		             double& reconstructedNextX,
		             bool& exactlyContiguous)
		{
			if (current.Command->Mapping.ShardIndex != next.Command->Mapping.ShardIndex ||
			    current.Command->BoundaryId != next.Command->BoundaryId ||
			    std::fabs(next.Command->Plan.VisualY - groupBaseline) > options.BaselineTolerance)
				return false;

			const double expectedX = reconstructedCurrentX + current.PdfAdvance;
			const double displacement = next.Command->Plan.VisualX - expectedX;
			const double exactAdjustment = -displacement * 1000.0;
			if (!std::isfinite(exactAdjustment) ||
			    std::fabs(exactAdjustment) > options.MaximumAbsoluteTjAdjustment)
				return false;

			exactlyContiguous = exactAdjustment == 0.0;
			adjustment = std::round(exactAdjustment * 100.0) / 100.0;
			reconstructedNextX = expectedX - adjustment / 1000.0;
			return std::fabs(reconstructedNextX - next.Command->Plan.VisualX) <=
			       options.PositionTolerance;
		}

		bool AppendChecked(std::string& output,
		                   const std::string& value,
		                   CLogicalTextSerializationError& error)
		{
			if (value.size() > MaximumContentSize - output.size())
				return SetError(error, CLogicalTextSerializationErrorCode::OutputTooLarge,
				                "logical text content exceeds the PDF stream size limit");
			output += value;
			return true;
		}
	}

	bool TrySerializeLogicalTextCommands(
		const std::vector<CLogicalTextCommand>& commands,
		const std::vector<CLogicalTextFontResource>& resources,
		const CLogicalTextSerializationOptions& options,
		std::string& content,
		CLogicalTextSerializationError& error)
	{
		error = CLogicalTextSerializationError();
		if (!std::isfinite(options.OriginX) || !std::isfinite(options.OriginY) ||
		    !std::isfinite(options.FontSize) || options.FontSize <= 0.0 ||
		    !std::isfinite(options.BaselineTolerance) || options.BaselineTolerance < 0.0 ||
		    !std::isfinite(options.PositionTolerance) || options.PositionTolerance < 0.0 ||
		    !std::isfinite(options.MaximumAbsoluteTjAdjustment) ||
		    options.MaximumAbsoluteTjAdjustment < 0.0)
			return SetError(error, CLogicalTextSerializationErrorCode::InvalidOptions,
			                "logical text serialization options are invalid");
		CLogicalTextSerializationOptions effectiveOptions = options;
		effectiveOptions.FontSize = RoundReal(options.FontSize, 6);
		if (effectiveOptions.FontSize <= 0.0)
			return SetError(error, CLogicalTextSerializationErrorCode::InvalidOptions,
			                "logical text font size rounds to zero in the PDF content stream");

		std::set<std::string> resourceNames;
		for (const CLogicalTextFontResource& resource : resources)
		{
			if (!IsValidResourceName(resource.Name) || resource.Font == nullptr ||
			    !resourceNames.insert(resource.Name).second)
				return SetError(error, CLogicalTextSerializationErrorCode::InvalidResource,
				                "logical font resources must be valid and uniquely named");
		}

		std::vector<CValidatedCommand> validated(commands.size());
		for (std::size_t index = 0; index < commands.size(); ++index)
		{
			if (!TryValidateCommand(commands[index], resources, index, validated[index], error))
				return false;
		}

		std::string output;
		std::size_t activeShard = std::numeric_limits<std::size_t>::max();
		std::size_t groupStart = 0;
		while (groupStart < validated.size())
		{
			const CValidatedCommand& first = validated[groupStart];
			if (activeShard != first.Command->Mapping.ShardIndex)
			{
				if (!AppendChecked(output, "/" + first.Resource->Name + " " +
				                          FormatReal(effectiveOptions.FontSize, 6) + " Tf\n", error))
					return false;
				activeShard = first.Command->Mapping.ShardIndex;
			}

			const double requestedGroupX = effectiveOptions.OriginX +
			                               first.Command->Plan.VisualX * effectiveOptions.FontSize;
			const double requestedGroupY = effectiveOptions.OriginY +
			                               first.Command->Plan.VisualY * effectiveOptions.FontSize;
			if (!std::isfinite(requestedGroupX) || !std::isfinite(requestedGroupY) ||
			    std::fabs(requestedGroupX) > MaximumAbsoluteUserCoordinate ||
			    std::fabs(requestedGroupY) > MaximumAbsoluteUserCoordinate)
				return SetError(error, CLogicalTextSerializationErrorCode::InvalidCommand,
				                "logical text matrix origin is outside the supported PDF coordinate range",
				                groupStart);
			const double groupX = RoundReal(requestedGroupX, 6);
			const double groupY = RoundReal(requestedGroupY, 6);
			const double reconstructedGroupX =
				(groupX - effectiveOptions.OriginX) / effectiveOptions.FontSize;
			const double reconstructedGroupY =
				(groupY - effectiveOptions.OriginY) / effectiveOptions.FontSize;
			if (std::fabs(reconstructedGroupX - first.Command->Plan.VisualX) >
			        effectiveOptions.PositionTolerance ||
			    std::fabs(reconstructedGroupY - first.Command->Plan.VisualY) >
			        effectiveOptions.BaselineTolerance)
				return SetError(error, CLogicalTextSerializationErrorCode::InsufficientPrecision,
				                "logical text matrix origin cannot be represented within tolerance",
				                groupStart);
			if (!AppendChecked(output, "1 0 0 1 " + FormatReal(groupX, 6) + " " +
			                          FormatReal(groupY, 6) + " Tm\n", error))
				return false;

			std::vector<double> adjustments;
			bool contiguous = true;
			double reconstructedCurrentX = reconstructedGroupX;
			const double groupBaseline = reconstructedGroupY;
			std::size_t groupEnd = groupStart + 1;
			while (groupEnd < validated.size())
			{
				double adjustment = 0.0;
				double reconstructedNextX = 0.0;
				bool exactlyContiguous = false;
				if (!CanJoin(validated[groupEnd - 1], validated[groupEnd], groupBaseline,
				             reconstructedCurrentX, effectiveOptions, adjustment, reconstructedNextX,
				             exactlyContiguous))
					break;
				adjustments.push_back(adjustment);
				contiguous = contiguous && exactlyContiguous;
				reconstructedCurrentX = reconstructedNextX;
				++groupEnd;
			}

			if (contiguous)
			{
				std::string codes;
				if (!AppendChecked(codes, "<", error))
					return false;
				for (std::size_t index = groupStart; index < groupEnd; ++index)
				{
					if (!AppendChecked(codes,
					                   FormatCid(validated[index].Command->Mapping.FontMapping.Cid), error))
						return false;
				}
				if (!AppendChecked(codes, "> Tj\n", error) || !AppendChecked(output, codes, error))
					return false;
			}
			else
			{
				std::string array;
				if (!AppendChecked(array,
				                   "[<" + FormatCid(first.Command->Mapping.FontMapping.Cid) + ">", error))
					return false;
				for (std::size_t index = groupStart + 1; index < groupEnd; ++index)
				{
					if (!AppendChecked(array,
					                   " " + FormatReal(adjustments[index - groupStart - 1], 2) + " <" +
					                       FormatCid(validated[index].Command->Mapping.FontMapping.Cid) + ">",
					                   error))
						return false;
				}
				if (!AppendChecked(array, "] TJ\n", error) || !AppendChecked(output, array, error))
					return false;
			}
			groupStart = groupEnd;
		}

		content = std::move(output);
		return true;
	}
}
