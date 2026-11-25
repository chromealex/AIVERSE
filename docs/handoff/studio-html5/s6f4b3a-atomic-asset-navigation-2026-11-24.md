# S6f.4b.3a — atomic Asset history navigation (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 51a19fb4f7bdd3507e943f41d001c05aee1494d7. Journal v4 9a95a41ca5320243aa826628df09a4ccc09bb02f; agent batch e3c8c395f909a520cf33f08be50db3a0e3c5510d; individual navigation 428bf72a245c8276656aceb95680b3ce0c2c845e; fixtures bcb8903e0509ce5a4d961a7d0260da96a420c65b.

- Outer batch journal v4 retains the exact current Asset checkpoint before payload changes. After actual final file state is available, it stages the next checkpoint and checksum-bound ready marker before publication; the outer commit determines the outcome of both cursor and payload. Recovery restores the previous checkpoint and files; metadata-only navigation uses v4 without relocations. Existing v1–v3 batch journals remain readable.
- Both individual Asset Undo/Redo and coordinated agent navigation use the paired-cursor API. Requires a valid existing checkpoint, exact baseline and no interrupted legacy marker. New asset mutations still record via the existing dirty-marker publication; first checkpoint creation and atomic mutation recording are not implemented in this increment.
- Fixtures cover relocation replay, restored persistent cursor, recovery prefixes before and after checkpoint publication, and metadata-only Undo/Redo. Source and fixtures committed; build, CTest, native QA and actual fault-injection execution NOT RUN. No main merge.

Next S6f.4b.3b: stage first checkpoint and newly recorded asset mutations with the same outer durability boundary; then S6h.3 recovery, S6i.3 MCP integration and QA.
