/*
 * (c) Copyright Ascensio System SIA 2010-2023
 *
 * This program is distributed under the GNU Affero General Public License
 * version 3. See the LICENSE file for details.
 */
#ifndef _PDF_WRITER_SRC_LOGICALMETAFILEADAPTER_H
#define _PDF_WRITER_SRC_LOGICALMETAFILEADAPTER_H

#include "LogicalText.h"
#include "../../DesktopEditor/graphics/IRenderer.h"

#include <cstddef>
#include <limits>
#include <string>

namespace PdfWriter
{
	enum class CLogicalMetafileAdapterErrorCode
	{
		None,
		InvalidUnitsPerEm,
		InvalidUnicode,
		InvalidGeometry,
		InvalidComponent,
		FontUnitOverflow
	};

	struct CLogicalMetafileAdapterError
	{
		static constexpr std::size_t NoItem = std::numeric_limits<std::size_t>::max();

		CLogicalMetafileAdapterErrorCode Code = CLogicalMetafileAdapterErrorCode::None;
		std::size_t ItemIndex = NoItem;
		std::string Message;
	};

	bool TryPlanLogicalMetafileUnit(
		const CRendererLogicalUnit& unit,
		unsigned int unitsPerEm,
		CLogicalUnitPlan& plan,
		CLogicalMetafileAdapterError& error);
}

#endif
