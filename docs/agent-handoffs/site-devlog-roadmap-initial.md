# Website navigation and roadmap maintenance

Base b6cbc3133. Consolidate public feeds in both languages, preserve bookmarks, compare roadmap claims to merged work and verify the generated site.

Stage 690bb24c0 recorded scope. Implementation 0adab8c02ffb48506a8cc6e53e8a01458ceb4061. Exact source paths are in docs/features/site-devlog-roadmap.json. Site validation found 72 entries and zero errors. The final build generated 152 pages across two locales and copied 164 media files. Chrome on macOS followed both /blog.html and /ru/blog.html with a topic and a post fragment, preserved the selected system or scrolled to the bookmarked post, and followed the render roadmap link to its filtered log. The render row lists eight milestones. Skill catalog check validated 302 skills. Next: MAIN squash, merged-tree Mac quick gate and site publication.

Integration 974d97c39 on main f7bc9bc51 retained both build roadmap milestones. The new bilingual build report is docs/evidence/site-devlog-roadmap-current.json. Next: verify the combined tree on Mac and publish.
