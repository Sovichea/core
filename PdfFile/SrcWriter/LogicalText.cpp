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
#include "LogicalText.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace PdfWriter
{
	namespace
	{
		bool SetError(CLogicalTextError& error, CLogicalTextErrorCode code,
		              const char* message, std::size_t itemIndex = CLogicalTextError::NoItem)
		{
			error.Code = code;
			error.ItemIndex = itemIndex;
			error.Message = message;
			return false;
		}

		bool IsUnicodeScalar(char32_t value)
		{
			return value <= 0x10FFFF && !(value >= 0xD800 && value <= 0xDFFF);
		}

		bool TryRoundFontUnits(double value, unsigned int unitsPerEm, int& result)
		{
			const double scaled = value * static_cast<double>(unitsPerEm);
			if (!std::isfinite(scaled))
				return false;

			const double rounded = std::round(scaled);
			if (rounded < static_cast<double>(std::numeric_limits<int>::min()) ||
			    rounded > static_cast<double>(std::numeric_limits<int>::max()))
				return false;

			result = static_cast<int>(rounded);
			return true;
		}
	}

	bool CLogicalTextLocation::operator==(const CLogicalTextLocation& other) const
	{
		return Start == other.Start && End == other.End;
	}

	bool CLogicalComponent::operator==(const CLogicalComponent& other) const
	{
		return GlyphId == other.GlyphId && X == other.X && Y == other.Y;
	}

	bool CLogicalComponent::operator<(const CLogicalComponent& other) const
	{
		return std::tie(GlyphId, X, Y) < std::tie(other.GlyphId, other.X, other.Y);
	}

	bool CVisualUnitKey::operator==(const CVisualUnitKey& other) const
	{
		return AdvanceWidth == other.AdvanceWidth && Components == other.Components;
	}

	bool CVisualUnitKey::operator<(const CVisualUnitKey& other) const
	{
		return std::tie(AdvanceWidth, Components) < std::tie(other.AdvanceWidth, other.Components);
	}

	bool CSemanticUnitKey::operator==(const CSemanticUnitKey& other) const
	{
		return Text == other.Text && Visual == other.Visual;
	}

	bool CSemanticUnitKey::operator<(const CSemanticUnitKey& other) const
	{
		return std::tie(Text, Visual) < std::tie(other.Text, other.Visual);
	}

	CSemanticUnitKey CLogicalUnitPlan::GetSemanticKey() const
	{
		return {Text, Visual};
	}

	bool TryPlanLogicalTextUnit(const CLogicalTextUnit& unit,
	                            unsigned int unitsPerEm,
	                            CLogicalUnitPlan& plan,
	                            CLogicalTextError& error)
	{
		plan = CLogicalUnitPlan();
		error = CLogicalTextError();

		if (unit.Text.empty())
			return SetError(error, CLogicalTextErrorCode::EmptyText,
			                "logical text must contain at least one Unicode scalar");

		for (std::size_t index = 0; index < unit.Text.size(); ++index)
		{
			if (!IsUnicodeScalar(unit.Text[index]))
				return SetError(error, CLogicalTextErrorCode::InvalidUnicodeScalar,
				                "logical text contains an invalid Unicode scalar", index);
		}

		if (unit.Glyphs.empty())
			return SetError(error, CLogicalTextErrorCode::EmptyGlyphs,
			                "logical text must contain at least one glyph");

		if (!std::isfinite(unit.VisualX) || !std::isfinite(unit.VisualY))
			return SetError(error, CLogicalTextErrorCode::NonFiniteVisualPosition,
			                "logical text visual position must be finite");

		if (unitsPerEm == 0)
			return SetError(error, CLogicalTextErrorCode::InvalidUnitsPerEm,
			                "units per em must be greater than zero");

		double penX = 0.0;
		double penY = 0.0;
		double minX = 0.0;
		double maxX = 0.0;
		std::vector<double> componentX;
		std::vector<double> componentY;
		componentX.reserve(unit.Glyphs.size());
		componentY.reserve(unit.Glyphs.size());

		for (std::size_t index = 0; index < unit.Glyphs.size(); ++index)
		{
			const CLogicalGlyph& glyph = unit.Glyphs[index];
			if (glyph.GlyphId == 0 || glyph.GlyphId > 0xFFFF)
				return SetError(error, CLogicalTextErrorCode::InvalidGlyphId,
				                "glyph id must be between 1 and 65535", index);

			if (!std::isfinite(glyph.XAdvance) || !std::isfinite(glyph.YAdvance) ||
			    !std::isfinite(glyph.XOffset) || !std::isfinite(glyph.YOffset))
				return SetError(error, CLogicalTextErrorCode::NonFiniteGlyphMetric,
				                "glyph advances and offsets must be finite", index);

			componentX.push_back(penX + glyph.XOffset);
			componentY.push_back(penY + glyph.YOffset);
			penX += glyph.XAdvance;
			penY += glyph.YAdvance;
			if (!std::isfinite(penX) || !std::isfinite(penY))
				return SetError(error, CLogicalTextErrorCode::FontUnitOverflow,
				                "glyph pen position overflowed", index);

			minX = std::min(minX, penX);
			maxX = std::max(maxX, penX);
		}

		CVisualUnitKey visual;
		if (!TryRoundFontUnits(maxX - minX, unitsPerEm, visual.AdvanceWidth))
			return SetError(error, CLogicalTextErrorCode::FontUnitOverflow,
			                "logical advance does not fit in font units");

		visual.Components.reserve(unit.Glyphs.size());
		for (std::size_t index = 0; index < unit.Glyphs.size(); ++index)
		{
			CLogicalComponent component;
			component.GlyphId = unit.Glyphs[index].GlyphId;
			if (!TryRoundFontUnits(componentX[index] - minX, unitsPerEm, component.X) ||
			    !TryRoundFontUnits(componentY[index], unitsPerEm, component.Y))
				return SetError(error, CLogicalTextErrorCode::FontUnitOverflow,
				                "glyph component position does not fit in font units", index);
			visual.Components.push_back(component);
		}

		plan.Text = unit.Text;
		plan.Visual = std::move(visual);
		plan.VisualX = unit.VisualX + minX;
		plan.VisualY = unit.VisualY;
		plan.Location = unit.Location;
		return true;
	}
}
