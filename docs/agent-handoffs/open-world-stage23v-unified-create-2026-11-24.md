# W1.23v

UnifiedWalSession::create initializes a new private empty directory, acquires lifetime lock, rechecks directory contents, stages initial world with reset sequence, publishes generation 1 and opens bound WAL before exposing state. Failed creation may leave files requiring explicit recovery; no automatic deletion. Native tests not run. Next selected generation loader and backup fork. PR #116 Draft; no main merge.
