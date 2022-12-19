---
sidebar_position: 3
---

# Limitations and Constraints

OI has been initially designed for use within Meta and therefore some of its current implementation may present challenges for you in your environment. We'd love to hear from you about what you need supporting and how any limitations effect you so please feel free to create issues on [GitHub](https://github.com/facebookexperimental/object-introspection) or tell us directly on [Matrix](https://matrix.to/#/#object-introspection:matrix.org) or [IRC](irc://irc.oftc.net/#object-introspection).

Current known limitations and constraints:

<ul>
	<li> Statically linked binaries only.</li>
	<li> Split Debug DWARF not currently supported (planned).</li>
	<li> C style unions not supported.</li>
	<li> Only supported architecture is x86-64.</li>
	<li> Only single probe specifications allowed in an oid invocation</li>
	<li> Virtual inheritance support</li>
	<li> Template specialization support</li>
	<li> Pluggable container support</li>
</ul>
