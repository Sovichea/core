cmake_minimum_required(VERSION 3.10)

project(pdf_logical_text_test)

set(CORE_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")

include(${CORE_ROOT_DIR}/common.cmake)

add_core_gtest(
    NAME pdf_logical_text_test
    SOURCES
        LogicalFontMapperTest.cpp
        LogicalTextTest.cpp
        LogicalSyntheticTrueTypeTest.cpp
        LogicalTrueTypeSubsetTest.cpp
        LogicalType0FontTest.cpp
        ${CORE_ROOT_DIR}/PdfFile/SrcWriter/LogicalFontMapper.cpp
        ${CORE_ROOT_DIR}/PdfFile/SrcWriter/LogicalText.cpp
        ${CORE_ROOT_DIR}/PdfFile/SrcWriter/LogicalTrueTypeSubset.cpp
        ${CORE_ROOT_DIR}/PdfFile/SrcWriter/LogicalType0Font.cpp
    GTEST_MAIN
)

target_include_directories(pdf_logical_text_test PRIVATE
    "${CORE_ROOT_DIR}/PdfFile/SrcWriter"
)

target_compile_definitions(pdf_logical_text_test PRIVATE
    EO_CORE_ROOT_DIR="${CORE_ROOT_DIR}"
)
