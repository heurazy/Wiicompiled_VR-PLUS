# PC installer disk and memory protection

The installer keeps an 8 GiB free-space reserve on the installation, Windows and temporary-file volumes. During installation and local compilation it checks these volumes once per second and cancels the child process tree when the reserve is exhausted or free space has decreased by more than 24 GiB since the operation started. It also stops when available Windows committed memory falls below 2 GiB. Interrupted operation staging is recovered under the installation lock before checking free space, allowing cleanup even on a nearly full drive.

Automatic compilation uses available physical memory, reserves 6 GiB for other applications, budgets 4 GiB per compiler and caps both native and translated compilation at four concurrent jobs. A developer's explicit `-Parallel` remains an override.

Retro Rewind downloads are limited to 4 GiB per archive, including HTTP responses without a Content-Length header. Extraction checks the total advertised size against an 8 GiB limit and remaining free space before writing. Each output is contained in the staging directory, respects cancellation, and cannot exceed the entry's advertised size. Future packs exceeding these limits require an installer update rather than consuming unbounded space.

The graphical installer uses classic Windows file/folder pickers and accepts pasted paths to avoid relying on modern shell picker extensions.

For a report of approximately 70 GB disk consumption, distinguish installed file size from Windows paging-file growth. The local September 27 investigation found a completed installation of approximately 7.1 GiB and Windows Resource-Exhaustion-Detector events naming RuntimeBroker.exe with 108–113 GiB of committed memory before unexpected restarts. This establishes a system memory leak during the reported period, but does not establish which application triggered it. The classic-picker change is a workaround to test, not proof that the external leak is resolved. Installer guards cannot stop a separate Windows process from leaking after installation has finished.
