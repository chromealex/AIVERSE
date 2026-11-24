# W1.23u

UnifiedWalSession adds openCheckpoint under single lifetime lock, verified loader then replay and writer open; managed checkpoint rotates generation under held lock, stops writer before publication, reopens bound WAL and fails closed on errors. Legacy explicit open remains unmanaged. Initial create API, backup integration, host materializer and resource WAL remain. No native tests run. PR #116 Draft; no main merge.
