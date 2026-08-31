#include "PdfFile.h"
#include "LogicalTrueTypeSubset.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace
{
	std::string SourceFontPath()
	{
		return std::string(EO_CORE_ROOT_DIR) +
		       "/DesktopEditor/freetype-2.10.4/docs/reference/assets/fonts/specimen/FontAwesome.ttf";
	}

	std::wstring Widen(const std::string& value)
	{
		return std::wstring(value.begin(), value.end());
	}

	std::vector<std::uint8_t> ReadBytes(const std::string& path)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream)
			return {};
		return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
		                                 std::istreambuf_iterator<char>());
	}

	std::string ReadText(const std::string& path)
	{
		const std::vector<std::uint8_t> bytes = ReadBytes(path);
		return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}
}

TEST(LogicalPdfWriterIntegration, WritesLogicalType0FontThroughProductionRenderer)
{
	const std::string sourcePath = SourceFontPath();
	const std::vector<std::uint8_t> source = ReadBytes(sourcePath);
	ASSERT_FALSE(source.empty());

	std::uint16_t advance = 0;
	PdfWriter::CLogicalTrueTypeSubsetError advanceError;
	ASSERT_TRUE(PdfWriter::TryGetTrueTypeGlyphAdvance(source, 13, advance, advanceError))
		<< advanceError.Message;

	NSFonts::IApplicationFonts* fonts = NSFonts::NSApplication::Create();
	ASSERT_NE(nullptr, fonts);
	const std::string::size_type separator = sourcePath.find_last_of("/\\");
	ASSERT_NE(std::string::npos, separator);
	fonts->InitializeFromFolder(Widen(sourcePath.substr(0, separator)));

	const std::string outputPath = "phase10-production-logical-unit.pdf";
	{
		CPdfFile pdf(fonts);
		pdf.CreatePdf();
		ASSERT_EQ(S_OK, pdf.NewPage());
		ASSERT_EQ(S_OK, pdf.put_FontPath(Widen(sourcePath)));
		ASSERT_EQ(S_OK, pdf.put_FontFaceIndex(0));
		ASSERT_EQ(S_OK, pdf.put_FontSize(24.0));
		ASSERT_EQ(S_OK, pdf.put_FontStringGID(TRUE));

		CRendererLogicalUnit unit;
		unit.Unicode = {0x0041, 0x0301};
		unit.VisualX = 20.0;
		unit.VisualY = 30.0;
		const double pointsPerMillimetre = 72.0 / 25.4;
		unit.LogicalAdvance = static_cast<double>(advance) / 1000.0 * 24.0 / pointsPerMillimetre;
		unit.Components.push_back({13, 0.0, 0.0});
		unit.Components.push_back({14, unit.LogicalAdvance * 0.25, 0.0});
		ASSERT_EQ(S_OK, pdf.CommandDrawTextLogicalUnit(unit));
		EXPECT_TRUE(pdf.GetLastLogicalTextDiagnostic().empty());

		ASSERT_EQ(S_OK, pdf.put_FontCharSpace(1.0));
		CRendererLogicalUnit fallbackUnit = unit;
		fallbackUnit.Unicode = {0x0042};
		fallbackUnit.Components.resize(1);
		fallbackUnit.VisualY = 45.0;
		ASSERT_EQ(S_OK, pdf.CommandDrawTextLogicalUnit(fallbackUnit));
		EXPECT_NE(std::string::npos,
		          pdf.GetLastLogicalTextDiagnostic().find("active text state is unsupported"));
		ASSERT_EQ(0, pdf.SaveToFile(Widen(outputPath)));
	}
	RELEASEINTERFACE(fonts);

	const std::string pdf = ReadText(outputPath);
	ASSERT_FALSE(pdf.empty());
	EXPECT_NE(std::string::npos, pdf.find("/Subtype /Type0"));
	EXPECT_NE(std::string::npos, pdf.find("/Encoding /Identity-H"));
	EXPECT_NE(std::string::npos, pdf.find("/Subtype /CIDFontType2"));
	EXPECT_NE(std::string::npos, pdf.find("/CIDToGIDMap"));
	EXPECT_NE(std::string::npos, pdf.find("/ToUnicode"));
	EXPECT_EQ(std::string::npos, pdf.find("/BaseFont /Helvetica"));
}
