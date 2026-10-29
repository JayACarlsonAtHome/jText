# Round trip through an OpenDocument spreadsheet (ODS.md), compared through the canonical
# XML export (XML_EXPORT.md): the fixture goes to .ods (jtext_to_ods) and back
# (jtext_from_ods), and the data-scope XML of the original and the copy must be
# byte-identical. With -DSOFFICE=<exe>, LibreOffice opens and saves the .ods in between
# (headless, with a throwaway profile), so a file Calc wrote is what is read back.
#
# Invoked via: cmake -DFIXTURE=<file> -DTOODS=<exe> -DFROMODS=<exe> -DTOXML=<exe>
#                    -DOUTDIR=<dir> -DNAME=<test name> [-DSOFFICE=<exe>] -P test_ods_round_trip.cmake

set(work "${OUTDIR}/${NAME}")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE so ERROR_VARIABLE se)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "${ARGN}\nexited ${rc}\n${so}${se}")
    endif()
endfunction()

run("${TOXML}" --data "${FIXTURE}" "${work}/original.xml")
run("${TOODS}" "${FIXTURE}" "${work}/sheet.ods")
set(ods "${work}/sheet.ods")
if(SOFFICE)
    file(MAKE_DIRECTORY "${work}/calc")
    run("${SOFFICE}" "-env:UserInstallation=file://${work}/profile" --headless --norestore
        --convert-to ods --outdir "${work}/calc" "${ods}")
    set(ods "${work}/calc/sheet.ods")
    if(NOT EXISTS "${ods}")
        message(FATAL_ERROR "LibreOffice wrote no ${ods}")
    endif()
endif()
run("${FROMODS}" "${ods}" "${work}/copy.jtext")
run("${TOXML}" --data "${work}/copy.jtext" "${work}/copy.xml")

file(READ "${work}/original.xml" original)
file(READ "${work}/copy.xml" copy)
if(NOT original STREQUAL copy)
    execute_process(COMMAND diff -u "${work}/original.xml" "${work}/copy.xml" OUTPUT_VARIABLE d)
    message(FATAL_ERROR "the .ods round trip changed the data:\n${d}")
endif()
message(STATUS "${NAME} OK: the data-scope XML of the original and the copy are identical")
