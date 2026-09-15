#import "../../book/lib.typ": *

= Index

Only places where this book *defines or directly covers* a concept are gathered. Places where a word merely passes by are not listed.

#v(0.4cm)

#if sys.inputs.at("mode", default: "paged") == "html" [
  This (HTML) edition has no page numbers, so only entries are listed. An index with page numbers is in the PDF edition.

  #make-index(pages: false)
] else [
  #make-index()
]
