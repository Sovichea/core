/*
 * (c) Copyright Ascensio System SIA 2010-2023
 *
 * This program is distributed under the GNU Affero General Public License
 * version 3. See http://www.gnu.org/licenses/agpl-3.0.html.
 */
#ifndef _PDF_WRITER_SRC_LOGICALPDFFONT_H
#define _PDF_WRITER_SRC_LOGICALPDFFONT_H

#include "Font.h"
#include "LogicalType0Font.h"

#include <string>
#include <vector>

namespace PdfWriter
{
	class CDocument;
	struct CLogicalPdfFontDescriptorMetrics;

	class CLogicalPdfFont : public CFontDict
	{
	public:
		static CLogicalPdfFont* Create(CXref* pXref,
		                               CDocument* pDocument,
		                               const CLogicalType0FontResult& result,
		                               const std::string& fontName,
		                               std::string* error = NULL);

		EFontType GetFontType() override
		{
			return fontCIDType2;
		}

		unsigned int GetWidth(unsigned short code) override;

	private:
		CLogicalPdfFont(CXref* pXref,
		                CDocument* pDocument,
		                const CLogicalType0FontResult& result,
		                const std::string& fontName,
		                const CLogicalPdfFontDescriptorMetrics& metrics);

		std::vector<int> m_widths;
	};
}

#endif
