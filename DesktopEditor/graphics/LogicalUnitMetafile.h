/*
 * (c) Copyright Ascensio System SIA 2010-2023
 *
 * This program is distributed under the GNU Affero General Public License
 * version 3. See the LICENSE file for details.
 */
#ifndef _BUILD_LOGICAL_UNIT_METAFILE_H_
#define _BUILD_LOGICAL_UNIT_METAFILE_H_

#include "IRenderer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace NSOnlineOfficeBinToPdf
{
	constexpr unsigned char LogicalUnitCommandVersion = 1;
	constexpr unsigned char LogicalUnitWritingModeVersion = 2;
	constexpr std::size_t MaximumLogicalUnitRecordBytes = 1024 * 1024;
	constexpr std::size_t MaximumLogicalUnitUnicodeScalars = 4096;
	constexpr std::size_t MaximumLogicalUnitComponents = 4096;

	enum class ELogicalUnitRecordResult
	{
		Parsed,
		UnsupportedVersion,
		Malformed
	};

	struct CLogicalUnitRecordError
	{
		std::size_t Offset = 0;
		std::string Message;
	};

	ELogicalUnitRecordResult ParseLogicalUnitRecord(
		const unsigned char* payload,
		std::size_t payloadSize,
		CRendererLogicalUnit& unit,
		CLogicalUnitRecordError* error = nullptr);

	bool SerializeLogicalUnitRecord(
		const CRendererLogicalUnit& unit,
		std::vector<unsigned char>& record,
		CLogicalUnitRecordError* error = nullptr);
}

#endif
