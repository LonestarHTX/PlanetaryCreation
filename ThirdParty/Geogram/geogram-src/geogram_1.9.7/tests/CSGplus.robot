*** Settings ***
Test Setup        Prepare Test
Test Teardown     Cleanup Test
Force Tags        CSGplus
Library           OperatingSystem
Library           String
Library           lib/VorpatestLibrary.py

*** Variables **
${DATADIR}        %{VORPATEST_ROOT_DIR}${/}data${/}CSG

*** Test Cases ***
example021.scad
    Run Test

example022.scad
    Run Test

example023.scad
    Run Test

example024.scad
    Run Test

*** Keywords ***
Run Test
    [Arguments]    ${input_name}=${TEST NAME}    @{options}
    [Documentation]    Computes a CSG operation
    ...    The name of the input file is taken from the test name.
    run command    compute_CSG  @{options}  ignore_cache_time=true  ${DATADIR}${/}${input_name}
