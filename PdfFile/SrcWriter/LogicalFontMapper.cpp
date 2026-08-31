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
#include "LogicalFontMapper.h"

namespace PdfWriter
{
	CLogicalFontMapping CLogicalFontShard::Map(const CLogicalUnitPlan& plan)
	{
		const CSemanticUnitKey semanticKey = plan.GetSemanticKey();
		const auto semantic = m_semanticCids.find(semanticKey);
		if (semantic != m_semanticCids.end())
		{
			const CLogicalCidRecord* record = GetCidRecord(semantic->second);
			return {semantic->second, record->VisualRecordId, false, false};
		}

		TLogicalVisualRecordId visualRecordId = 0;
		bool visualCreated = false;
		const auto visual = m_visualRecordIds.find(plan.Visual);
		if (visual != m_visualRecordIds.end())
		{
			visualRecordId = visual->second;
		}
		else
		{
			visualRecordId = static_cast<TLogicalVisualRecordId>(m_visualRecords.size() + 1);
			m_visualRecordIds.emplace(plan.Visual, visualRecordId);
			m_visualRecords.push_back({visualRecordId, plan.Visual});
			visualCreated = true;
		}

		const TLogicalCid cid = static_cast<TLogicalCid>(m_cidRecords.size() + 1);
		m_semanticCids.emplace(semanticKey, cid);
		m_cidRecords.push_back({cid, plan.Text, visualRecordId, plan.Visual.AdvanceWidth});
		return {cid, visualRecordId, true, visualCreated};
	}

	std::size_t CLogicalFontShard::GetSemanticCount() const
	{
		return m_cidRecords.size();
	}

	std::size_t CLogicalFontShard::GetVisualCount() const
	{
		return m_visualRecords.size();
	}

	const CLogicalCidRecord* CLogicalFontShard::GetCidRecord(TLogicalCid cid) const
	{
		if (cid == 0 || cid > m_cidRecords.size())
			return nullptr;
		return &m_cidRecords[static_cast<std::size_t>(cid - 1)];
	}

	const CLogicalVisualRecord* CLogicalFontShard::GetVisualRecord(TLogicalVisualRecordId visualRecordId) const
	{
		if (visualRecordId == 0 || visualRecordId > m_visualRecords.size())
			return nullptr;
		return &m_visualRecords[static_cast<std::size_t>(visualRecordId - 1)];
	}

	CLogicalFontMapping CLogicalFontMapper::Map(const CLogicalUnitPlan& plan)
	{
		return m_shard.Map(plan);
	}

	const CLogicalFontShard& CLogicalFontMapper::GetShard() const
	{
		return m_shard;
	}
}
