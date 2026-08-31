cmake_minimum_required(VERSION 3.10)

project(pdf_logical_text_test)

set(CORE_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")

include(${CORE_ROOT_DIR}/common.cmake)

add_core_gtest(
    NAME pdf_logical_text_test
    SOURCES
        LogicalFontMapperTest.cpp
        LogicalFontShardingTest.cpp
        LogicalMetafileAdapterTest.cpp
        LogicalUnitMetafileTest.cpp
        LogicalTextTest.cpp
        LogicalTextPdfTest.cpp
        LogicalTextSerializerTest.cpp
        LogicalSyntheticTrueTypeTest.cpp
        LogicalTrueTypeSubsetTest.cpp
        LogicalType0FontTest.cpp
        ${CORE_ROOT_DIR}/PdfFile/SrcWriter/LogicalFontMapper.cpp
        ${CORE_ROOT_DIR}/PdfFile/SrcWriter/LogicalFontSharding.cpp
        ${CORE_ROOT_DIR}/PdfFile/SrcWriter/LogicalMetafileAdapter.cpp
        ${CORE_ROOT_DIR}/DesktopEditor/graphics/LogicalUnitMetafile.cpp
        ${CORE_ROOT_DIR}/PdfFile/SrcWriter/LogicalText.cpp
        ${CORE_ROOT_DIR}/PdfFile/SrcWriter/LogicalTextSerializer.cpp
        ${CORE_ROOT_DIR}/PdfFile/SrcWriter/LogicalTrueTypeSubset.cpp
        ${CORE_ROOT_DIR}/PdfFile/SrcWriter/LogicalType0Font.cpp
    GTEST_MAIN
)

target_include_directories(pdf_logical_text_test PRIVATE
    "${CORE_ROOT_DIR}/DesktopEditor/graphics"
    "${CORE_ROOT_DIR}/PdfFile/SrcWriter"
)

target_compile_definitions(pdf_logical_text_test PRIVATE
    EO_CORE_ROOT_DIR="${CORE_ROOT_DIR}"
)
