"use strict";(self.webpackChunkoi_web=self.webpackChunkoi_web||[]).push([[678],{3905:(e,t,n)=>{n.d(t,{Zo:()=>p,kt:()=>m});var r=n(7294);function o(e,t,n){return t in e?Object.defineProperty(e,t,{value:n,enumerable:!0,configurable:!0,writable:!0}):e[t]=n,e}function i(e,t){var n=Object.keys(e);if(Object.getOwnPropertySymbols){var r=Object.getOwnPropertySymbols(e);t&&(r=r.filter((function(t){return Object.getOwnPropertyDescriptor(e,t).enumerable}))),n.push.apply(n,r)}return n}function a(e){for(var t=1;t<arguments.length;t++){var n=null!=arguments[t]?arguments[t]:{};t%2?i(Object(n),!0).forEach((function(t){o(e,t,n[t])})):Object.getOwnPropertyDescriptors?Object.defineProperties(e,Object.getOwnPropertyDescriptors(n)):i(Object(n)).forEach((function(t){Object.defineProperty(e,t,Object.getOwnPropertyDescriptor(n,t))}))}return e}function s(e,t){if(null==e)return{};var n,r,o=function(e,t){if(null==e)return{};var n,r,o={},i=Object.keys(e);for(r=0;r<i.length;r++)n=i[r],t.indexOf(n)>=0||(o[n]=e[n]);return o}(e,t);if(Object.getOwnPropertySymbols){var i=Object.getOwnPropertySymbols(e);for(r=0;r<i.length;r++)n=i[r],t.indexOf(n)>=0||Object.prototype.propertyIsEnumerable.call(e,n)&&(o[n]=e[n])}return o}var l=r.createContext({}),c=function(e){var t=r.useContext(l),n=t;return e&&(n="function"==typeof e?e(t):a(a({},t),e)),n},p=function(e){var t=c(e.components);return r.createElement(l.Provider,{value:t},e.children)},d="mdxType",u={inlineCode:"code",wrapper:function(e){var t=e.children;return r.createElement(r.Fragment,{},t)}},b=r.forwardRef((function(e,t){var n=e.components,o=e.mdxType,i=e.originalType,l=e.parentName,p=s(e,["components","mdxType","originalType","parentName"]),d=c(n),b=o,m=d["".concat(l,".").concat(b)]||d[b]||u[b]||i;return n?r.createElement(m,a(a({ref:t},p),{},{components:n})):r.createElement(m,a({ref:t},p))}));function m(e,t){var n=arguments,o=t&&t.mdxType;if("string"==typeof e||o){var i=n.length,a=new Array(i);a[0]=b;var s={};for(var l in t)hasOwnProperty.call(t,l)&&(s[l]=t[l]);s.originalType=e,s[d]="string"==typeof e?e:o,a[1]=s;for(var c=2;c<i;c++)a[c]=n[c];return r.createElement.apply(null,a)}return r.createElement.apply(null,n)}b.displayName="MDXCreateElement"},7520:(e,t,n)=>{n.r(t),n.d(t,{assets:()=>l,contentTitle:()=>a,default:()=>d,frontMatter:()=>i,metadata:()=>s,toc:()=>c});var r=n(7462),o=(n(7294),n(3905));const i={title:"A Simple Address Book Example"},a="A simple address book example",s={unversionedId:"addrbook-intro",id:"addrbook-intro",title:"A Simple Address Book Example",description:"Let's start with a very simple C++ application: an address book. This contrived simple piece of code contains everything we need to take you through the basics of using OI. The code itself can be found in the examples/web/AddrBook directory in the OI GitHub repo.",source:"@site/docs/addrbook-intro.md",sourceDirName:".",slug:"/addrbook-intro",permalink:"/docs/addrbook-intro",draft:!1,editUrl:"https://github.com/facebookexperimental/object-introspection/docs/addrbook-intro.md",tags:[],version:"current",frontMatter:{title:"A Simple Address Book Example"},sidebar:"mysidebar",previous:{title:"Getting Started",permalink:"/docs/getting-started"},next:{title:"this pointers",permalink:"/docs/addrbook-this"}},l={},c=[],p={toc:c};function d(e){let{components:t,...n}=e;return(0,o.kt)("wrapper",(0,r.Z)({},p,n,{components:t,mdxType:"MDXLayout"}),(0,o.kt)("h1",{id:"a-simple-address-book-example"},"A simple address book example"),(0,o.kt)("p",null,"Let's start with a very simple C++ application: an address book. This contrived simple piece of code contains everything we need to take you through the basics of using OI. The code itself can be found in the ",(0,o.kt)("inlineCode",{parentName:"p"},"examples/web/AddrBook")," directory in the OI ",(0,o.kt)("a",{parentName:"p",href:"https://github.com/facebookexperimental/object-introspection/tree/main/examples/web/AddrBook"},"GitHub repo"),"."),(0,o.kt)("p",null,"First, build the test application:"),(0,o.kt)("pre",null,(0,o.kt)("code",{parentName:"pre"},"$ ~/object-introspection/examples/web/AddrBook: make CC=clang++-12\nclang++-12 -o addrbook AddrBook.cpp -std=c++20 -g -O3\n")),(0,o.kt)("p",null,"(No need to override the 'CC' make variable if you have ",(0,o.kt)("inlineCode",{parentName:"p"},"clang++")," in your path)."),(0,o.kt)("p",null,"You can see the DWARF data is present in the generated executable:"),(0,o.kt)("pre",null,(0,o.kt)("code",{parentName:"pre"},'$ ~/object-introspection/examples/web/AddrBook# size -At addrbook | grep "\\.debug"\n.debug_info           71316         0\n.debug_abbrev          2446         0\n.debug_line            8971         0\n.debug_str            44990         0\n.debug_loc            27968         0\n.debug_ranges         10240         0\n')),(0,o.kt)("p",null,"Each address book is composed of a single ",(0,o.kt)("inlineCode",{parentName:"p"},"AddressBook")," object which contains zero or more ",(0,o.kt)("inlineCode",{parentName:"p"},"Contact")," objects. Here's how the data and interface definitions look for the two objects:"),(0,o.kt)("pre",null,(0,o.kt)("code",{parentName:"pre",className:"language-C++"},"class Contact {\npublic:\n  Contact(std::string& f, std::string& l, std::string& n);\nprivate:\n  std::string firstName, lastName;\n  std::string number;\n};\n\nclass AddressBook {\npublic:\n  void AddContact(std::string& f, std::string& l, std::string& n);\n  void DumpContacts(void);\nprivate:\n  int rev;\n  std::string Owner;\n  std::vector<Contact> Entries;\n};\n")),(0,o.kt)("p",null,"OI can introspect objects at specific points in an application:"),(0,o.kt)("ul",null,(0,o.kt)("li",null,"Function arguments upon entry to a function."),(0,o.kt)("li",null,"Function arguments upon return to a function."),(0,o.kt)("li",null,"The return value from a function."),(0,o.kt)("li",null,"This `this` pointer at entry or return from an object method."),(0,o.kt)("li",null,"Global objects.")),(0,o.kt)("p",null,"Let's get started by introspecting an object using its ",(0,o.kt)("inlineCode",{parentName:"p"},"this")," pointer."))}d.isMDXComponent=!0}}]);