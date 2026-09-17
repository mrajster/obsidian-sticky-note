---
title: "Torture Test — Obsidian Flavored Markdown"
aliases: [torture, "md fixture", Ščžđ-unicode]
tags: [test/fixture, "quoted tag"]
created: 2026-09-16T10:00:00+02:00
cssclasses:
  - wide-table
publish: false
not_a_task: "- [ ] this MUST NOT be detected as a task (frontmatter scalar)"
fake_fence: "``` this is not a code fence ```"
block_scalar: |
  - [ ] still not a task (YAML block scalar)
  # not a heading, not a tag
  ---
list_of_maps:
  - key: value
    other: "[[Not A Wikilink]]"
---

# Torture Test

Intro paragraph with **bold**, _italic_, ==highlight==, ~~strike~~, `inline code`,
a hard break via two trailing spaces →  
and a backslash break →\
done.

## 1. Links, embeds, aliases

- [[Simple Wikilink]]
- [[Note Title|Display Alias]]
- [[Folder/Sub Folder/Note#Heading|alias with spaces]]
- [[Note#^block-id-1]]
- [[#Local Heading]]
- ![[Embedded Note]]
- ![[image with spaces.png|300x200]]
- ![[Embedded Note#Section]]
- ![[Embedded Note#^block-id-1]]
- ![[audio.mp3]] and ![[doc.pdf#page=3]]
- [regular md link](Other%20Note.md) and <https://example.com/a_b_c?x=1&y=2>
- ![alt text](https://example.com/i.png "title")
- [[Note with [brackets] inside]]
- escaped not-a-link: \[\[not a wikilink\]\]

## 2. Tags

#tag #nested/tag/deep #tag-with-dash #tag_with_underscore #2026-planning #Ščžđ
Inline prose tag: see #project/alpha for details.
NOT tags: C# and F#, colour #ffcc00, issue #12345 (pure digits), a # alone,
and `#in-inline-code`, and this heading marker is not a tag either.

## 3. Callouts

> [!note] Callout title with [[wikilink]] and ==highlight==
> Body line one.
>
> Body line after an empty quote line.
> - [ ] task inside a callout
> - [x] done inside a callout

> [!warning]- Collapsed by default (minus)
> Hidden body.

> [!tip]+ Expanded by default (plus)
> Visible body.
> > [!danger] Nested callout
> > Nested body with $x^2$ math.

> [!quote]
> Callout with no title line.

## 4. Block references

This paragraph is individually referenceable. ^block-id-1

A second target with digits and dashes. ^20260916-142355

- List item block ref ^list-block

| a | b |
|---|---|
| 1 | 2 |
^table-block-ref

## 5. Footnotes

Standard footnote reference.[^1] Named one.[^note-name] Inline one.^[inline footnote body with [[link]]]

[^1]: The footnote definition.
    Indented continuation line of the same footnote (4 spaces — NOT a code block).

    Second paragraph of the footnote.
[^note-name]: Named footnote with `code` and #tag.

## 6. Math (LaTeX)

Inline: $E = mc^2$, $a_i * b_j$, $\alpha_{i,j} \in \{0,1\}$, $\|x\|_2$.
Not math (currency): I paid $5 and then $10 for it.
Escaped dollar: \$notmath\$

$$
\begin{bmatrix}
a & b \\
c & d
\end{bmatrix}
\quad \sum_{i=0}^{n} x_i^2 \leq \epsilon
$$

$$P(A \mid B) = \frac{P(B \mid A)\,P(A)}{P(B)}$$

## 7. Dataview

status:: in-progress
due:: 2026-09-20
Inline paren field (rating:: 9) and inline bracket field [priority:: high] in prose.

```dataview
TABLE file.mtime AS "Modified", status
FROM #test/fixture
WHERE contains(status, "in-progress")
SORT due ASC
```

```dataviewjs
dv.taskList(dv.pages("#test/fixture").file.tasks.where(t => !t.completed));
```

## 8. Tasks — every accepted form

- [ ] plain unchecked, dash marker
- [x] checked, lowercase x
- [X] checked, uppercase X
* [ ] asterisk marker
+ [ ] plus marker
1. [ ] ordered marker with dot
2) [x] ordered marker with paren
10. [ ] two-digit ordered marker
-   [ ] multiple spaces between marker and bracket
-	[ ] tab between marker and bracket
- [ ]
- [x] trailing content with two spaces for a hard break  
- [/] in progress (custom state)
- [-] cancelled (custom state)
- [>] forwarded / deferred (custom state)
- [<] scheduled (custom state)
- [?] question (custom state)
- [!] important (custom state)
- ["] quote (custom state, double-quote char)
- [*] star (custom state, asterisk char)
- [I] idea (custom state, uppercase non-X)
- [b] bookmark
- [k] key
- [u] up
- [d] down
- [ ] task with emoji metadata 🔺 📅 2026-09-20 ✅ 2026-09-16 🔁 every week
- [ ] task with [[Note|alias]], #tag, ==highlight==, $x^2$, `inline code`, and ^task-block-ref
- [ ] task containing literal brackets `- [x] inside inline code`
	- [ ] TAB-indented subtask (depth 1)
		- [x] TAB-indented subtask (depth 2)
  - [ ] two-space indented subtask
      - [>] six-space indented sub-subtask
- [ ] parent
    - [ ] four-space indented child (inside list context — IS a task)
- [ ] unicode body — preveri ščžđ in emoji 🚀

> - [ ] task inside a blockquote (not a callout)
> - [x] done inside a blockquote

## 9. NOT tasks (must never match)

-[ ] no space after list marker
- [] empty brackets
- [ x] leading space inside brackets
- [x ] trailing space inside brackets
- [xx] two characters inside brackets
[ ] bare brackets with no list marker
[x] bare checked brackets with no list marker
- [x]no space after the closing bracket
- [Link text](https://example.com) markdown link in a list item
- [[Wikilink In A List Item]]
- [!note] looks like a callout marker but is a plain list item
Prose mentioning - [ ] mid-sentence should not match.
`- [ ] inline code task` at start of a paragraph line.

## 10. Code fences and code blocks

```python
# - [ ] not a task, this is a comment
checkbox = "- [x] also not a task"
front = """
---
title: not frontmatter
---
"""
```

```
- [ ] plain fence with no info string, not a task
#not-a-tag [[not-a-wikilink]] ==not-highlight==
```

~~~text
- [ ] tilde fence, not a task
```
- [x] a backtick line inside a tilde fence
~~~

````markdown
```python
- [ ] nested fence content, not a task
```
- [ ] still inside the outer four-backtick fence, not a task
````

```js title="example.js" {1,3-4}
// fence with an attribute-laden info string that must survive byte-for-byte
const s = "- [ ] not a task";
```

   ```bash
   # fence indented by 3 spaces (still a valid fence)
   echo "- [ ] not a task"
   ```

An indented code block follows after this blank line:

    - [ ] four-space indented code block at top level, NOT a task
    echo "still code"

## 11. Tables

| Feature | Syntax | Works? | Notes |
| --- | :--- | :---: | ---: |
| Wikilink | `[[Note]]` | yes | pipes inside code need escaping |
| Escaped pipe | a \| b | yes | literal pipe |
| Embed | `![[img.png]]` | yes | |
| Math | $a_1$ | yes | underscore must not become emphasis |
| Task-ish | `- [ ] x` | n/a | not a task, it is a cell |

| Ragged | table | without | padding |
|---|---|---|---|
| 1 | 2 | 3 | 4 |

## 12. Misc

%%
A multi-line Obsidian comment.
- [ ] not a task (inside a comment block)
%%

Inline %%comment%% in prose.

<div align="center" data-x="1">
  Raw HTML block with a <b>bold</b> child and a - [ ] pseudo task.
</div>

Line with trailing whitespace for a hard break →  
next line.

---

***

___

- [ ] final task after thematic breaks

## 13. Fences nested inside list items

- Item with a nested fence:
    ```python
    - [ ] not a task, fenced inside a list item
    checkbox = "- [x] also not a task"
    ```
- Item with a deeper tilde fence:
  - Nested item:
      ~~~text
      - [ ] not a task, tilde-fenced inside a nested list item
      ```
      - [x] backticks never close a tilde fence
      ~~~

## 14. Fence closing rules

````
```
- [ ] three backticks cannot close a four-backtick fence
````

## 15. List item continuation paragraphs

- A list item whose body continues after a blank line.

    Continuation paragraph with [[Continued Wikilink]] that must be linkified.

    - [ ] continuation task inside the same list item

Plain paragraph that closes the list.
