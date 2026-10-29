# HTML round trip: the fixture as an HTML page (jtext_to_html, scope SCOPE), read back
# (jtext_from_html), and both the fixture and the copy exported as canonical XML. The two
# XML files must be byte-identical: the page held every value, declaration and comment.
#
# Invoked via: cmake -DTOXML=<exe> -DTOHTML=<exe> -DFROMHTML=<exe> -DSCOPE=full|data
#                    -DFIXTURE=<.jtFull> -DOUTDIR=<dir> -P test_html_round_trip.cmake

get_filename_component(name "${FIXTURE}" NAME_WE)
set(work "${OUTDIR}/html_round_trip_${name}_${SCOPE}")
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
run("${TOHTML}" ${flag} "${FIXTURE}" "${work}/page.html")
run("${FROMHTML}" "${work}/page.html" "${work}/copy.jtFull")
run("${TOXML}" ${flag} "${work}/copy.jtFull" "${work}/copy.xml")

file(READ "${work}/original.xml" original)
file(READ "${work}/copy.xml" copy)
if(NOT original STREQUAL copy)
    execute_process(COMMAND diff -u "${work}/original.xml" "${work}/copy.xml" OUTPUT_VARIABLE d)
    message(FATAL_ERROR "the HTML round trip changed the data (${SCOPE} scope):\n${d}")
endif()
