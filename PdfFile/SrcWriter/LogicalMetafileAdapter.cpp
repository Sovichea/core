/*
 * (c) Copyright Ascensio System SIA 2010-2023
 *
 * This program is distributed under the GNU Affero General Public License
 * version 3. See the LICENSE file for details.
 */
#include "LogicalMetafileAdapter.h"

#include <cmath>
#include <cstdint>

namespace PdfWriter
{
	namespace
	{
		bool SetError(CLogicalMetafileAdapterError& error,
		              CLogicalMetafileAdapterErrorCode code,
		              const char* message,
		              std::size_t itemIndex = CLogicalMetafileAdapterError::NoItem)
		{
			error.Code = code;
			error.ItemIndex = itemIndex;
			error.Message = message;
			return false;
		}

		bool IsUnicodeScalar(std::uint32_t value)
		{
			return value <= 0x10FFFFu && !(value >= 0xD800u && value <= 0xDFFFu);
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

	bool TryPlanLogicalMetafileUnit(
		const CRendererLogicalUnit& unit,
		unsigned int unitsPerEm,
		CLogicalUnitPlan& plan,
		CLogicalMetafileAdapterError& error)
	{
		error = CLogicalMetafileAdapterError();
		if (unitsPerEm == 0)
			return SetError(error, CLogicalMetafileAdapterErrorCode::InvalidUnitsPerEm,
			                "logical metafile units per em must be greater than zero");
		if (unit.Unicode.empty())
			return SetError(error, CLogicalMetafileAdapterErrorCode::InvalidUnicode,
			                "logical metafile unit has no Unicode scalars");

		CLogicalUnitPlan parsed;
		parsed.Text.reserve(unit.Unicode.size());
		for (std::size_t index = 0; index < unit.Unicode.size(); ++index)
		{
			if (!IsUnicodeScalar(unit.Unicode[index]))
				return SetError(error, CLogicalMetafileAdapterErrorCode::InvalidUnicode,
				                "logical metafile unit contains an invalid Unicode scalar", index);
			parsed.Text.push_back(static_cast<char32_t>(unit.Unicode[index]));
		}

		if (!std::isfinite(unit.LogicalAdvance) || unit.LogicalAdvance < 0.0 ||
		    !std::isfinite(unit.VisualX) || !std::isfinite(unit.VisualY))
			return SetError(error, CLogicalMetafileAdapterErrorCode::InvalidGeometry,
			                "logical metafile geometry is invalid");
		if (!TryRoundFontUnits(unit.LogicalAdvance, unitsPerEm, parsed.Visual.AdvanceWidth))
			return SetError(error, CLogicalMetafileAdapterErrorCode::FontUnitOverflow,
			                "logical metafile advance does not fit in font units");
		if (unit.Components.empty())
			return SetError(error, CLogicalMetafileAdapterErrorCode::InvalidComponent,
			                "logical metafile unit has no components");

		parsed.Visual.Components.reserve(unit.Components.size());
		for (std::size_t index = 0; index < unit.Components.size(); ++index)
		{
			const CRendererLogicalComponent& source = unit.Components[index];
			if (source.SourceGid == 0 || source.SourceGid > 0xFFFFu ||
			    !std::isfinite(source.RelativeX) || !std::isfinite(source.RelativeY))
				return SetError(error, CLogicalMetafileAdapterErrorCode::InvalidComponent,
				                "logical metafile component is invalid", index);
			CLogicalComponent component;
			component.GlyphId = source.SourceGid;
			if (!TryRoundFontUnits(source.RelativeX, unitsPerEm, component.X) ||
			    !TryRoundFontUnits(-source.RelativeY, unitsPerEm, component.Y))
				return SetError(error, CLogicalMetafileAdapterErrorCode::FontUnitOverflow,
				                "logical metafile component does not fit in font units", index);
			parsed.Visual.Components.push_back(component);
		}
		parsed.VisualX = unit.VisualX;
		parsed.VisualY = unit.VisualY;
		plan = std::move(parsed);
		return true;
	}
}
