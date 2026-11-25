fbcae35a5786cd45098aff0ba5601ac9480876d5
ClientUi previously restricted the candidate set to four history entries before expiring them. The visible list now uses active indices, allowing a long-running notice to return when shorter messages expire. Zero-duration messages enter history only. GameClient resets active indices alongside match history. Runtime/UI acceptance pending.
