# W1.22g — world session lock

RegionWorldSessionLock.h adds noncopyable RAII exclusive lifetime lock using POSIX flock or Win32 no-sharing CreateFileW. Existing private stable directory required; never unlink or replace lock file. Advisory POSIX lock requires cooperating callers; not a distributed lock. Acquire before manifest load and retain through recovery, writer use, rotation and shutdown. Primitive only: existing low-level APIs do not enforce ownership; next compose session owner and bind WAL identity. No native tests run. PR #116 Draft; no main merge.
