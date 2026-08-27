# Camera ETL validation (selected metadata)

The completed RGB and fresh Hello retry are included. The interrupted Hello run and first incomplete audit are excluded. Raw ETLs/XML remain private.

| ETL | Native events | Events lost | Buffer loss | XML camera/schema errors | Requested providers emitting |
| --- | ---: | ---: | --- | --- | --- |
| camera-front-providers.etl | 129782 | 0 | 0 | 0 camera; 1 trace metadata | 7/9 |
| camera-front-video.etl | 5445172 | 0 | unknown | summary only | WPR profile |
| camera-rear-providers.etl | 52818 | 0 | 0 | 0 camera; 1 trace metadata | 6/9 |
| camera-rear-video.etl | 6963080 | 0 | unknown | summary only | WPR profile |
| camera-ir-hello-providers.etl | 3037 | 0 | 0 | 0 camera; 1 trace metadata | 8/11 |

Every source ETL matched its original capture manifest and was unchanged during the audit. Audit manifests and payload hashes were independently checked before this projection. The two captures retain their `partial-inventory` status; those inventory errors are separate from ETW transport and decoding.

## Time and schema caveats

The provider CSV maps requested GUIDs to the names saved during discovery, includes silent providers, and uses independent `Get-WinEvent` metadata for UTC bounds. No payload values or device/account identifiers were exported.

Hello XML used `+00:59`; all eight emitting-provider bounds are 60 seconds later than the independent reader. RGB XML mixes `+00:59` and `+01:00` within each file, so its first/last boundary differences are not uniform. Do not apply a single time shift. The CSV uses independent ISO8601 UTC timestamps with fractional precision; XML remains unchanged and flagged. No event bounds prove phase continuity.

Native, XML element, XML parser and Get-WinEvent counts are separate columns in the JSON. Classic trace headers can be exposed differently. ProcessingErrorData counts do not automatically mean lost camera events. WPR was not payload-decoded, so its buffer-loss/schema/register coverage remains unknown.

## Register evidence

No attributable CCI/register transaction sequence has been validated. Selected field-name triage is only a lead, not a bus address/value/direction/completion trace. Zero transport loss, advertised controls and operator-reported Hello success do not establish Linux camera parity.

Source hashes, per-provider counts and safe field names are in `etl-summary.sanitized.json` and `etl-providers.sanitized.csv`. Verification records are in `etl-projection-verification.json`.
