/*
 * (c) Copyright Ascensio System SIA 2010-2023
 *
 * This program is distributed under the GNU Affero General Public License
 * version 3. See the LICENSE file for details.
 */
#ifndef _PDF_WRITER_SRC_LOGICALTEXTSERIALIZER_H
#define _PDF_WRITER_SRC_LOGICALTEXTSERIALIZER_H

#include "LogicalFontSharding.h"
#include "LogicalType0Font.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace PdfWriter
{
	struct CLogicalTextCommand
	{
		CLogicalUnitPlan Plan;
		CShardedLogicalFontMapping Mapping;
		std::uint64_t BoundaryId = 0;
	};

	struct CLogicalTextFontResource
	{
		std::string Name;
		const CLogicalType0FontResult* Font = nullptr;
	};

	struct CLogicalTextSerializationOptions
	{
		double OriginX = 0.0;
		double OriginY = 0.0;
		double FontSize = 1.0;
		double BaselineTolerance = 0.000005;
		double PositionTolerance = 0.000005;
		double MaximumAbsoluteTjAdjustment = 10000000.0;
	};

	enum class CLogicalTextSerializationErrorCode
	{
		None,
		InvalidOptions,
		InvalidResource,
		InvalidCommand,
		InvalidMapping,
		InvalidWidth,
		InsufficientPrecision,
		OutputTooLarge
	};

	struct CLogicalTextSerializationError
	{
		static constexpr std::size_t NoCommand = std::numeric_limits<std::size_t>::max();

		CLogicalTextSerializationErrorCode Code = CLogicalTextSerializationErrorCode::None;
		std::size_t CommandIndex = NoCommand;
		std::string Message;
	};

	bool TrySerializeLogicalTextCommands(
		const std::vector<CLogicalTextCommand>& commands,
		const std::vector<CLogicalTextFontResource>& resources,
		const CLogicalTextSerializationOptions& options,
		std::string& content,
		CLogicalTextSerializationError& error);
}

#endif
