/*
 * (c) Copyright Ascensio System SIA 2010-2023
 *
 * This program is distributed under the GNU Affero General Public License
 * version 3. See http://www.gnu.org/licenses/agpl-3.0.html.
 */
#ifndef _PDF_LOGICAL_TEXT_METRICS_H
#define _PDF_LOGICAL_TEXT_METRICS_H

#include <cstddef>

struct CLogicalTextMetrics
{
	std::size_t UnitsReceived = 0;
	std::size_t LogicalUnits = 0;
	std::size_t FallbackUnits = 0;
	std::size_t SourceFonts = 0;
	std::size_t Shards = 0;
	std::size_t SemanticCids = 0;
	std::size_t VisualRecords = 0;
	std::size_t EmbeddedGids = 0;
	std::size_t FontPublications = 0;
	std::size_t FinalEmbeddedFontBytes = 0;
};

#endif
