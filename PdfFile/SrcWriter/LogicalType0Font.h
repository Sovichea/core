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
#ifndef _PDF_WRITER_SRC_LOGICALTYPE0FONT_H
#define _PDF_WRITER_SRC_LOGICALTYPE0FONT_H

#include "LogicalTrueTypeSubset.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace PdfWriter
{
	struct CLogicalType0FontMetadata
	{
		static constexpr const char* FontType = "Font";
		static constexpr const char* FontSubtype = "Type0";
		static constexpr const char* Encoding = "Identity-H";
		static constexpr const char* DescendantFontSubtype = "CIDFontType2";
	};

	enum class CLogicalType0FontErrorCode
	{
		None,
		TooManyCids,
		InvalidCidRecord,
		InvalidVisualRecord,
		InvalidUnicodeScalar,
		SubsetBuildFailed,
		InvalidSubsetResult,
		OutputTooLarge
	};

	struct CLogicalType0FontError
	{
		static constexpr TLogicalCid NoCid = 0;
		static constexpr std::size_t NoTextIndex = std::numeric_limits<std::size_t>::max();

		CLogicalType0FontErrorCode Code = CLogicalType0FontErrorCode::None;
		TLogicalCid Cid = NoCid;
		std::size_t TextIndex = NoTextIndex;
		CLogicalTrueTypeSubsetError SubsetError;
		std::string Message;
	};

	struct CLogicalType0FontResult
	{
		static constexpr const char* FontType = CLogicalType0FontMetadata::FontType;
		static constexpr const char* FontSubtype = CLogicalType0FontMetadata::FontSubtype;
		static constexpr const char* Encoding = CLogicalType0FontMetadata::Encoding;
		static constexpr const char* DescendantFontSubtype = CLogicalType0FontMetadata::DescendantFontSubtype;

		std::vector<std::uint8_t> FontFile2;
		std::vector<std::uint8_t> CIDToGIDMap;
		std::vector<int> Widths;
		std::string ToUnicode;
	};

	bool TryBuildLogicalType0Font(const std::vector<std::uint8_t>& source,
	                              const CLogicalFontShard& shard,
	                              CLogicalType0FontResult& result,
	                              CLogicalType0FontError& error,
	                              const std::string& fontName = std::string());
}

#endif
