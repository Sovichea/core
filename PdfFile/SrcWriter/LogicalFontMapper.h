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
#ifndef _PDF_WRITER_SRC_LOGICALFONTMAPPER_H
#define _PDF_WRITER_SRC_LOGICALFONTMAPPER_H

#include "LogicalText.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>

namespace PdfWriter
{
	using TLogicalCid = std::uint32_t;
	using TLogicalVisualRecordId = std::uint32_t;

	struct CLogicalVisualRecord
	{
		TLogicalVisualRecordId Id = 0;
		CVisualUnitKey Visual;
	};

	struct CLogicalCidRecord
	{
		TLogicalCid Cid = 0;
		std::u32string Text;
		TLogicalVisualRecordId VisualRecordId = 0;
		int Width = 0;
	};

	struct CLogicalFontMapping
	{
		TLogicalCid Cid = 0;
		TLogicalVisualRecordId VisualRecordId = 0;
		bool SemanticCreated = false;
		bool VisualCreated = false;
	};

	class CLogicalFontShard
	{
	public:
		CLogicalFontMapping Map(const CLogicalUnitPlan& plan);

		std::size_t GetSemanticCount() const;
		std::size_t GetVisualCount() const;
		const CLogicalCidRecord* GetCidRecord(TLogicalCid cid) const;
		const CLogicalVisualRecord* GetVisualRecord(TLogicalVisualRecordId visualRecordId) const;

	private:
		std::map<CSemanticUnitKey, TLogicalCid> m_semanticCids;
		std::map<CVisualUnitKey, TLogicalVisualRecordId> m_visualRecordIds;
		std::deque<CLogicalCidRecord> m_cidRecords;
		std::deque<CLogicalVisualRecord> m_visualRecords;
	};

	class CLogicalFontMapper
	{
	public:
		CLogicalFontMapping Map(const CLogicalUnitPlan& plan);
		const CLogicalFontShard& GetShard() const;

	private:
		CLogicalFontShard m_shard;
	};
}

#endif
