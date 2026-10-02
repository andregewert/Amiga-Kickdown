# Edge cases for the syntax highlighting

snake_case_names stay plain, 2*3*4 is emphasis, *a **b** c* nests.
Unmatched ** markers and a lone * star, \*escaped\* too.
Code ``with ` backtick`` and `*no emphasis* <b>inside</b>`.
Mail <someone@example.com>, entity &amp; and a [ref link][ref].

~~~
inside a tilde fence: **not bold**
```
still inside
~~~

    indented code (not highlighted)

> quote with **bold** and `code`
>> nested quote

1) other list marker
10. two digits

Setext heading
==============

Setext h2
---

[ref]: http://example.com "Title"
[^note]: footnote with *emphasis*
