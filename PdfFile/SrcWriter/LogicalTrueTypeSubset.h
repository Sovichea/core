/*
 * (c) Copyright Ascensio System SIA 2010-2023
 *
 * This program is a free software product. You can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License (AGPL)
 * version 3 as published by the Free Software Foundation. In accordance with
 * Section 7(a) of the GNU AGPL its Section 15 shall be amended to the effect
 * that Ascensio System SIA expressly excludes the warranty of non-infringement
 * of any third-party rights.
 *
 * This program is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. For
 * details, see the GNU AGPL at: http://www.gnu.org/licenses/agpl-3.0.html
 *
 * The interactive user interfaces in modified source and object code versions
 * of the Program must display Appropriate Legal Notices, as required under
 * Section 5 of the GNU AGPL version 3.
 *
 * All the Product's GUI elements, including illustrations and icon sets, as
 * well as technical writing content are licensed under the terms of the
 * Creative Commons Attribution-ShareAlike 4.0 International. See the License
 * terms at http://creativecommons.org/licenses/by-sa/4.0/legalcode
 */
#ifndef _PDF_WRITER_SRC_LOGICALTRUETYPESUBSET_H
#define _PDF_WRITER_SRC_LOGICALTRUETYPESUBSET_H

#include "LogicalFontMapper.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace PdfWriter
{
	enum class CLogicalTrueTypeSubsetErrorCode
	{
		None,
		UnsupportedCollection,
		UnsupportedFontFlavor,
		UnsupportedVariableFont,
		MalformedSfnt,
		MissingRequiredTable,
		MalformedHead,
		MalformedHhea,
		MalformedMaxp,
		MalformedHmtx,
		MalformedLoca,
		MalformedGlyf,
		InvalidVisualRecord,
		UnsupportedVisual,
		InvalidSourceGlyph,
		AdvanceWidthMismatch,
		ComponentCoordinateOverflow,
		SyntheticBoundsOverflow,
		SyntheticResourceOverflow,
		InvalidAdvanceWidth,
		InvalidCidRecord,
		CompositeCycle,
		TooManyGlyphs,
		OutputTooLarge
	};

	struct CLogicalTrueTypeSubsetError
	{
		static constexpr TLogicalVisualRecordId NoVisualRecord = 0;
		static constexpr std::uint32_t NoSourceGlyph = std::numeric_limits<std::uint32_t>::max();

		CLogicalTrueTypeSubsetErrorCode Code = CLogicalTrueTypeSubsetErrorCode::None;
		TLogicalVisualRecordId VisualRecordId = NoVisualRecord;
		std::uint32_t SourceGlyphId = NoSourceGlyph;
		std::string Message;
	};

	struct CLogicalTrueTypeSubsetResult
	{
		static constexpr std::uint32_t UnmappedGlyph = std::numeric_limits<std::uint32_t>::max();

		std::vector<std::uint8_t> FontData;
		std::uint16_t UnitsPerEm = 0;
		std::vector<std::uint32_t> SourceGidToSubsetGid;
		std::vector<std::uint32_t> VisualRecordToSubsetGid;
		std::vector<bool> VisualRecordIsSynthetic;
		std::vector<std::uint32_t> CidToSubsetGid;
	};

	bool TryBuildSourceBackedLogicalTrueType(const std::vector<std::uint8_t>& source,
	                                         const CLogicalFontShard& shard,
	                                         CLogicalTrueTypeSubsetResult& result,
	                                         CLogicalTrueTypeSubsetError& error);

	bool TryBuildLogicalTrueType(const std::vector<std::uint8_t>& source,
	                             const CLogicalFontShard& shard,
	                             CLogicalTrueTypeSubsetResult& result,
	                             CLogicalTrueTypeSubsetError& error);

	bool TryGetTrueTypeGlyphAdvance(const std::vector<std::uint8_t>& fontData,
	                                std::uint32_t glyphId,
	                                std::uint16_t& advanceWidth,
	                                CLogicalTrueTypeSubsetError& error);
}

#endif
