/*
 * (c) Copyright Ascensio System SIA 2010-2023
 *
 * This program is distributed under the GNU Affero General Public License
 * version 3. See http://www.gnu.org/licenses/agpl-3.0.html.
 */
#include "LogicalPdfFont.h"

#include "Consts.h"
#include "Streams.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>

namespace PdfWriter
{
	struct CLogicalPdfFontDescriptorMetrics
	{
		int BBox[4] = {0, 0, 0, 0};
		int Ascent = 0;
		int Descent = 0;
		int CapHeight = 0;
		int ItalicAngle = 0;
		int Flags = 4;
		int FontWeight = 400;
		int StemV = 0;
	};

	namespace
	{
		struct CTable
		{
			std::size_t Offset = 0;
			std::size_t Length = 0;
		};

		constexpr std::uint32_t MakeTag(char a, char b, char c, char d)
		{
			return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24) |
			       (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16) |
			       (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8) |
			       static_cast<std::uint32_t>(static_cast<unsigned char>(d));
		}

		bool HasRange(std::size_t size, std::size_t offset, std::size_t length)
		{
			return offset <= size && length <= size - offset;
		}

		std::uint16_t ReadU16(const std::vector<std::uint8_t>& data, std::size_t offset)
		{
			return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8) |
			                                  static_cast<std::uint16_t>(data[offset + 1]));
		}

		std::int16_t ReadS16(const std::vector<std::uint8_t>& data, std::size_t offset)
		{
			return static_cast<std::int16_t>(ReadU16(data, offset));
		}

		std::uint32_t ReadU32(const std::vector<std::uint8_t>& data, std::size_t offset)
		{
			return (static_cast<std::uint32_t>(data[offset]) << 24) |
			       (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
			       (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
			       static_cast<std::uint32_t>(data[offset + 3]);
		}

		std::int32_t ReadS32(const std::vector<std::uint8_t>& data, std::size_t offset)
		{
			return static_cast<std::int32_t>(ReadU32(data, offset));
		}

		int ScaleMetric(std::int32_t value, std::uint16_t unitsPerEm)
		{
			const std::int64_t scaled = static_cast<std::int64_t>(value) * 1000;
			if (scaled >= 0)
				return static_cast<int>((scaled + unitsPerEm / 2) / unitsPerEm);
			return static_cast<int>((scaled - unitsPerEm / 2) / unitsPerEm);
		}

		bool Fail(std::string* error, const char* message)
		{
			if (error)
				*error = message;
			return false;
		}

		bool ParseMetrics(const std::vector<std::uint8_t>& data,
		                  CLogicalPdfFontDescriptorMetrics& metrics,
		                  std::string* error)
		{
			if (!HasRange(data.size(), 0, 12))
				return Fail(error, "FontFile2 has a truncated SFNT header");
			const std::uint32_t flavor = ReadU32(data, 0);
			if (flavor != 0x00010000u && flavor != MakeTag('t', 'r', 'u', 'e'))
				return Fail(error, "FontFile2 is not a TrueType outlines SFNT");

			const std::uint16_t tableCount = ReadU16(data, 4);
			if (tableCount == 0 || !HasRange(data.size(), 12, static_cast<std::size_t>(tableCount) * 16))
				return Fail(error, "FontFile2 has a truncated table directory");

			std::map<std::uint32_t, CTable> tables;
			for (std::size_t index = 0; index < tableCount; ++index)
			{
				const std::size_t record = 12 + index * 16;
				const std::uint32_t tag = ReadU32(data, record);
				const std::size_t offset = ReadU32(data, record + 8);
				const std::size_t length = ReadU32(data, record + 12);
				if (!HasRange(data.size(), offset, length))
					return Fail(error, "FontFile2 contains a table outside the SFNT data");
				if (!tables.emplace(tag, CTable{offset, length}).second)
					return Fail(error, "FontFile2 contains a duplicate table tag");
			}

			const auto headIt = tables.find(MakeTag('h', 'e', 'a', 'd'));
			const auto hheaIt = tables.find(MakeTag('h', 'h', 'e', 'a'));
			const auto postIt = tables.find(MakeTag('p', 'o', 's', 't'));
			if (headIt == tables.end() || headIt->second.Length < 54)
				return Fail(error, "FontFile2 has no complete head table");
			if (hheaIt == tables.end() || hheaIt->second.Length < 36)
				return Fail(error, "FontFile2 has no complete hhea table");
			if (postIt == tables.end() || postIt->second.Length < 16)
				return Fail(error, "FontFile2 has no complete post table");

			const std::size_t head = headIt->second.Offset;
			if (ReadU32(data, head + 12) != 0x5F0F3CF5u)
				return Fail(error, "FontFile2 head table has an invalid magic number");
			const std::uint16_t unitsPerEm = ReadU16(data, head + 18);
			if (unitsPerEm < 16 || unitsPerEm > 16384)
				return Fail(error, "FontFile2 head.unitsPerEm is outside the TrueType range");

			metrics.BBox[0] = ScaleMetric(ReadS16(data, head + 36), unitsPerEm);
			metrics.BBox[1] = ScaleMetric(ReadS16(data, head + 38), unitsPerEm);
			metrics.BBox[2] = ScaleMetric(ReadS16(data, head + 40), unitsPerEm);
			metrics.BBox[3] = ScaleMetric(ReadS16(data, head + 42), unitsPerEm);
			if (metrics.BBox[0] > metrics.BBox[2] || metrics.BBox[1] > metrics.BBox[3])
				return Fail(error, "FontFile2 head table has an invalid bounding box");

			const std::size_t hhea = hheaIt->second.Offset;
			metrics.Ascent = ScaleMetric(ReadS16(data, hhea + 4), unitsPerEm);
			metrics.Descent = ScaleMetric(ReadS16(data, hhea + 6), unitsPerEm);
			metrics.CapHeight = metrics.BBox[3];

			const std::size_t post = postIt->second.Offset;
			const std::int32_t italicFixed = ReadS32(data, post + 4);
			metrics.ItalicAngle = static_cast<int>(italicFixed / 65536);
			if (ReadU32(data, post + 12) != 0)
				metrics.Flags |= 1;
			if (italicFixed != 0)
				metrics.Flags |= 64;

			const auto os2It = tables.find(MakeTag('O', 'S', '/', '2'));
			if (os2It != tables.end())
			{
				const CTable& os2 = os2It->second;
				if (os2.Length < 8)
					return Fail(error, "FontFile2 has a truncated OS/2 table");
				metrics.FontWeight = std::max(1, std::min(1000, static_cast<int>(ReadU16(data, os2.Offset + 4))));
				if (os2.Length >= 64 && (ReadU16(data, os2.Offset + 62) & 1u) != 0)
					metrics.Flags |= 64;
				const std::uint16_t version = ReadU16(data, os2.Offset);
				if (version >= 2 && os2.Length >= 90)
					metrics.CapHeight = ScaleMetric(ReadS16(data, os2.Offset + 88), unitsPerEm);
			}

			// PDF has no direct TrueType stem metric. This weight-based Adobe-style
			// estimate remains font-derived and avoids inventing per-font constants.
			metrics.StemV = std::max(50, std::min(200,
				50 + metrics.FontWeight * metrics.FontWeight / 6500));
			return true;
		}

		bool ValidateResult(const CLogicalType0FontResult& result,
		                    const std::string& fontName,
		                    CLogicalPdfFontDescriptorMetrics& metrics,
		                    std::string* error)
		{
			if (fontName.empty() || fontName.size() > LIMIT_MAX_NAME_LEN || fontName.find('\0') != std::string::npos)
				return Fail(error, "logical PDF font name is empty or exceeds the PDF name limit");
			if (result.FontFile2.empty())
				return Fail(error, "logical PDF font has an empty FontFile2");
			if (result.FontFile2.size() > std::numeric_limits<unsigned int>::max() ||
			    result.CIDToGIDMap.size() > std::numeric_limits<unsigned int>::max() ||
			    result.ToUnicode.size() > std::numeric_limits<unsigned int>::max())
				return Fail(error, "logical PDF font stream exceeds the writer size limit");
			if (result.CIDToGIDMap.size() < 2 || (result.CIDToGIDMap.size() & 1u) != 0)
				return Fail(error, "logical PDF font has an invalid CIDToGIDMap length");
			if (result.CIDToGIDMap[0] != 0 || result.CIDToGIDMap[1] != 0)
				return Fail(error, "logical PDF font maps reserved CID 0 to a nonzero GID");
			if (result.Widths.size() != result.CIDToGIDMap.size() / 2)
				return Fail(error, "logical PDF font widths do not match its CID count");
			if (result.ToUnicode.empty())
				return Fail(error, "logical PDF font has an empty ToUnicode CMap");
			return ParseMetrics(result.FontFile2, metrics, error);
		}

		void WriteBytes(CDictObject* dictionary, const std::uint8_t* data, std::size_t size)
		{
			dictionary->SetFilter(STREAM_FILTER_FLATE_DECODE);
			if (size != 0)
				dictionary->GetStream()->Write(reinterpret_cast<const BYTE*>(data), static_cast<unsigned int>(size));
		}
	}

	CLogicalPdfFont* CLogicalPdfFont::Create(CXref* pXref,
	                                       CDocument* pDocument,
	                                       const CLogicalType0FontResult& result,
	                                       const std::string& fontName,
	                                       std::string* error)
	{
		if (error)
			error->clear();
		if (!pXref || !pDocument)
		{
			Fail(error, "logical PDF font requires an initialized document");
			return NULL;
		}

		CLogicalPdfFontDescriptorMetrics metrics;
		if (!ValidateResult(result, fontName, metrics, error))
			return NULL;
		return new CLogicalPdfFont(pXref, pDocument, result, fontName, metrics);
	}

	CLogicalPdfFont::CLogicalPdfFont(CXref* pXref,
	                                 CDocument* pDocument,
	                                 const CLogicalType0FontResult& result,
	                                 const std::string& fontName,
	                                 const CLogicalPdfFontDescriptorMetrics& metrics)
		: CFontDict(pXref, pDocument), m_widths(result.Widths)
	{
		Add("Type", CLogicalType0FontResult::FontType);
		Add("Subtype", CLogicalType0FontResult::FontSubtype);
		Add("BaseFont", fontName.c_str());
		Add("Encoding", CLogicalType0FontResult::Encoding);

		CDictObject* descendant = new CDictObject();
		pXref->Add(descendant);
		descendant->Add("Type", "Font");
		descendant->Add("Subtype", CLogicalType0FontResult::DescendantFontSubtype);
		descendant->Add("BaseFont", fontName.c_str());
		descendant->Add("DW", 1000);

		CDictObject* cidSystemInfo = new CDictObject();
		cidSystemInfo->Add("Registry", new CStringObject("Adobe"));
		cidSystemInfo->Add("Ordering", new CStringObject("Identity"));
		cidSystemInfo->Add("Supplement", 0);
		descendant->Add("CIDSystemInfo", cidSystemInfo);

		if (result.Widths.size() > 1)
		{
			CArrayObject* widths = new CArrayObject();
			widths->Add(1);
			CArrayObject* consecutiveWidths = new CArrayObject();
			for (std::size_t cid = 1; cid < result.Widths.size(); ++cid)
				consecutiveWidths->Add(result.Widths[cid]);
			widths->Add(consecutiveWidths);
			descendant->Add("W", widths);
		}

		CDictObject* descriptor = new CDictObject();
		pXref->Add(descriptor);
		descriptor->Add("Type", "FontDescriptor");
		descriptor->Add("FontName", fontName.c_str());
		descriptor->Add("Flags", metrics.Flags);
		CArrayObject* bbox = new CArrayObject();
		for (int value : metrics.BBox)
			bbox->Add(value);
		descriptor->Add("FontBBox", bbox);
		descriptor->Add("ItalicAngle", metrics.ItalicAngle);
		descriptor->Add("Ascent", metrics.Ascent);
		descriptor->Add("Descent", metrics.Descent);
		descriptor->Add("CapHeight", metrics.CapHeight);
		descriptor->Add("StemV", metrics.StemV);
		descriptor->Add("FontWeight", metrics.FontWeight);

		CDictObject* fontFile = new CDictObject(pXref);
		fontFile->Add("Length1", static_cast<unsigned int>(result.FontFile2.size()));
		WriteBytes(fontFile, result.FontFile2.data(), result.FontFile2.size());
		descriptor->Add("FontFile2", fontFile);
		descendant->Add("FontDescriptor", descriptor);

		CDictObject* cidToGid = new CDictObject(pXref);
		WriteBytes(cidToGid, result.CIDToGIDMap.data(), result.CIDToGIDMap.size());
		descendant->Add("CIDToGIDMap", cidToGid);

		CArrayObject* descendants = new CArrayObject();
		descendants->Add(descendant);
		Add("DescendantFonts", descendants);

		CDictObject* toUnicode = new CDictObject(pXref);
		WriteBytes(toUnicode, reinterpret_cast<const std::uint8_t*>(result.ToUnicode.data()), result.ToUnicode.size());
		Add("ToUnicode", toUnicode);
	}

	unsigned int CLogicalPdfFont::GetWidth(unsigned short code)
	{
		if (code >= m_widths.size() || m_widths[code] <= 0)
			return 0;
		return static_cast<unsigned int>(m_widths[code]);
	}
}
