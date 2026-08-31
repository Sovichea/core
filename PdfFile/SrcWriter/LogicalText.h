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
#ifndef _PDF_WRITER_SRC_LOGICALTEXT_H
#define _PDF_WRITER_SRC_LOGICALTEXT_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace PdfWriter
{
	struct CLogicalTextLocation
	{
		std::uint64_t Start = 0;
		std::uint64_t End = 0;

		bool operator==(const CLogicalTextLocation& other) const;
	};

	struct CLogicalGlyph
	{
		unsigned int GlyphId = 0;
		double XAdvance = 0.0;
		double YAdvance = 0.0;
		double XOffset = 0.0;
		double YOffset = 0.0;
	};

	struct CLogicalTextUnit
	{
		std::u32string Text;
		std::vector<CLogicalGlyph> Glyphs;
		double VisualX = 0.0;
		double VisualY = 0.0;
		std::optional<CLogicalTextLocation> Location;
	};

	struct CLogicalComponent
	{
		unsigned int GlyphId = 0;
		int X = 0;
		int Y = 0;

		bool operator==(const CLogicalComponent& other) const;
		bool operator<(const CLogicalComponent& other) const;
	};

	struct CVisualUnitKey
	{
		int AdvanceWidth = 0;
		std::vector<CLogicalComponent> Components;

		bool operator==(const CVisualUnitKey& other) const;
		bool operator<(const CVisualUnitKey& other) const;
	};

	struct CSemanticUnitKey
	{
		std::u32string Text;
		CVisualUnitKey Visual;

		bool operator==(const CSemanticUnitKey& other) const;
		bool operator<(const CSemanticUnitKey& other) const;
	};

	struct CLogicalUnitPlan
	{
		std::u32string Text;
		CVisualUnitKey Visual;
		double VisualX = 0.0;
		double VisualY = 0.0;
		std::optional<CLogicalTextLocation> Location;

		CSemanticUnitKey GetSemanticKey() const;
	};

	enum class CLogicalTextErrorCode
	{
		None,
		EmptyText,
		InvalidUnicodeScalar,
		EmptyGlyphs,
		InvalidGlyphId,
		NonFiniteVisualPosition,
		NonFiniteGlyphMetric,
		InvalidUnitsPerEm,
		FontUnitOverflow
	};

	struct CLogicalTextError
	{
		static constexpr std::size_t NoItem = std::numeric_limits<std::size_t>::max();

		CLogicalTextErrorCode Code = CLogicalTextErrorCode::None;
		std::size_t ItemIndex = NoItem;
		std::string Message;
	};

	bool TryPlanLogicalTextUnit(const CLogicalTextUnit& unit,
	                            unsigned int unitsPerEm,
	                            CLogicalUnitPlan& plan,
	                            CLogicalTextError& error);
}

#endif
