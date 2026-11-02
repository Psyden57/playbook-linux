# Security policy

This is a hardware and boot-chain research project.  Flashing an image, writing
the eMMC or NVRAM, or touching the RPMB store can permanently brick a unit.
There is no warranty; see `LICENSE`.

## Reporting a problem

If you find a vulnerability in the host tooling or a reproducible way to damage a
unit that the documentation does not already warn about, use GitHub's private
vulnerability reporting on this repository (Security → Report a vulnerability)
rather than a public issue.

Do not include live device secrets, RPMB records or personal data in a report.
The project's own documentation describes the hardware risks; a report is for a
new, unexpected issue in the code.
