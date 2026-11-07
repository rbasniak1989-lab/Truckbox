# Embedded browser regression

Run node tests/web/test_flow.cjs with Playwright and Chromium available.

The test serves the actual embedded HTML against a local mock HTTP server. It never calls a device or the production API. Coverage includes public state, authentication, partial configuration, password omission/clearing, distinct GPS credentials, sequence limits, Core/Link controls, pairing, image input checks, OTA routing/progress/errors, logout and mobile overflow.

result.json and screenshots are local test artifacts. They do not prove physical firmware flashing, rollback, Wi-Fi RF behavior, or vehicle/modem operation.
