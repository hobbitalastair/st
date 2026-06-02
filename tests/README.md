# End to end tests

To run the tests:
`make test`

To update the screenshots:
`make update-screenshots`


## Test coverage

Mostly for detecting regressions.

Verify that the application:
- runs/quits
- renders OK
- handles different escape sequences OK
- renders emoji OK


## Implementation

Uses pytest.
Tests are run in parallel where possible.
Screenshots are stored in a folder.
Screenshots from failed test runs are also retained so that it's easy to compare and see what changed.


## Tools

For running `st`:
`WLR_BACKENDS=headless cage -- <path to st>`

Taking screenshots:
use grim (FIXME: replace with a full command)

Providing input:
`wlrctl keyboard type <text>`
`wlrctl pointer move <right dist> <down dist>`

Consider using `bwrap` to isolate each test run.

