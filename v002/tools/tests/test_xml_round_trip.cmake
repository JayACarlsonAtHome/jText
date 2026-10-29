# XML import round trip: the fixture is exported (jtext_to_xml, scope SCOPE), imported back
# (jtext_from_xml), and the copy exported again. The two XML files must be byte-identical:
# the import gave back every value, declaration and comment the export holds.
#
# Invoked via: cmake -DTOXML=<exe> -DFROMXML=<exe> -DSCOPE=full|data -DFIXTURE=<.jtFull>
#                    -DOUTDIR=<dir> -P test_xml_round_trip.cmake

get_filename_component(name "${FIXTURE}" NAME_WE)
set(work "${OUTDIR}/xml_round_trip_${name}_${SCOPE}")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")
if(SCOPE STREQUAL "data")
    set(flag --data)
else()
    set(flag "")
endif()

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE so ERROR_VARIABLE se)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "${ARGN}\nexited ${rc}\n${so}${se}")
    endif()
endfunction()

run("${TOXML}" ${flag} "${FIXTURE}" "${work}/original.xml")
run("${FROMXML}" "${work}/original.xml" "${work}/copy.jtFull")
run("${TOXML}" ${flag} "${work}/copy.jtFull" "${work}/copy.xml")

file(READ "${work}/original.xml" original)
file(READ "${work}/copy.xml" copy)
if(NOT original STREQUAL copy)
    execute_process(COMMAND diff -u "${work}/original.xml" "${work}/copy.xml" OUTPUT_VARIABLE d)
    message(FATAL_ERROR "the XML import changed the data (${SCOPE} scope):\n${d}")
endif()
