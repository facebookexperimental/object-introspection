"use strict";(self.webpackChunkoi_web=self.webpackChunkoi_web||[]).push([[983],{3905:(e,t,n)=>{n.d(t,{Zo:()=>u,kt:()=>m});var r=n(7294);function o(e,t,n){return t in e?Object.defineProperty(e,t,{value:n,enumerable:!0,configurable:!0,writable:!0}):e[t]=n,e}function i(e,t){var n=Object.keys(e);if(Object.getOwnPropertySymbols){var r=Object.getOwnPropertySymbols(e);t&&(r=r.filter((function(t){return Object.getOwnPropertyDescriptor(e,t).enumerable}))),n.push.apply(n,r)}return n}function a(e){for(var t=1;t<arguments.length;t++){var n=null!=arguments[t]?arguments[t]:{};t%2?i(Object(n),!0).forEach((function(t){o(e,t,n[t])})):Object.getOwnPropertyDescriptors?Object.defineProperties(e,Object.getOwnPropertyDescriptors(n)):i(Object(n)).forEach((function(t){Object.defineProperty(e,t,Object.getOwnPropertyDescriptor(n,t))}))}return e}function s(e,t){if(null==e)return{};var n,r,o=function(e,t){if(null==e)return{};var n,r,o={},i=Object.keys(e);for(r=0;r<i.length;r++)n=i[r],t.indexOf(n)>=0||(o[n]=e[n]);return o}(e,t);if(Object.getOwnPropertySymbols){var i=Object.getOwnPropertySymbols(e);for(r=0;r<i.length;r++)n=i[r],t.indexOf(n)>=0||Object.prototype.propertyIsEnumerable.call(e,n)&&(o[n]=e[n])}return o}var l=r.createContext({}),c=function(e){var t=r.useContext(l),n=t;return e&&(n="function"==typeof e?e(t):a(a({},t),e)),n},u=function(e){var t=c(e.components);return r.createElement(l.Provider,{value:t},e.children)},p="mdxType",f={inlineCode:"code",wrapper:function(e){var t=e.children;return r.createElement(r.Fragment,{},t)}},d=r.forwardRef((function(e,t){var n=e.components,o=e.mdxType,i=e.originalType,l=e.parentName,u=s(e,["components","mdxType","originalType","parentName"]),p=c(n),d=o,m=p["".concat(l,".").concat(d)]||p[d]||f[d]||i;return n?r.createElement(m,a(a({ref:t},u),{},{components:n})):r.createElement(m,a({ref:t},u))}));function m(e,t){var n=arguments,o=t&&t.mdxType;if("string"==typeof e||o){var i=n.length,a=new Array(i);a[0]=d;var s={};for(var l in t)hasOwnProperty.call(t,l)&&(s[l]=t[l]);s.originalType=e,s[p]="string"==typeof e?e:o,a[1]=s;for(var c=2;c<i;c++)a[c]=n[c];return r.createElement.apply(null,a)}return r.createElement.apply(null,n)}d.displayName="MDXCreateElement"},1105:(e,t,n)=>{n.r(t),n.d(t,{assets:()=>l,contentTitle:()=>a,default:()=>p,frontMatter:()=>i,metadata:()=>s,toc:()=>c});var r=n(7462),o=(n(7294),n(3905));const i={sidebar_position:3},a="Limitations and Constraints",s={unversionedId:"constraints",id:"constraints",title:"Limitations and Constraints",description:"OI has been initially designed for use within Meta and therefore some of its current implementation may present challenges for you in your environment. We'd love to hear from you about what you need supporting and how any limitations effect you so please feel free to create issues on GitHub or tell us directly on Matrix or IRC.",source:"@site/docs/constraints.md",sourceDirName:".",slug:"/constraints",permalink:"/docs/constraints",draft:!1,editUrl:"https://github.com/facebookexperimental/object-introspection/docs/constraints.md",tags:[],version:"current",sidebarPosition:3,frontMatter:{sidebar_position:3},sidebar:"mysidebar",previous:{title:"Function Arguments",permalink:"/docs/addrbook-funcargs"},next:{title:"Contributing",permalink:"/docs/contributing"}},l={},c=[],u={toc:c};function p(e){let{components:t,...n}=e;return(0,o.kt)("wrapper",(0,r.Z)({},u,n,{components:t,mdxType:"MDXLayout"}),(0,o.kt)("h1",{id:"limitations-and-constraints"},"Limitations and Constraints"),(0,o.kt)("p",null,"OI has been initially designed for use within Meta and therefore some of its current implementation may present challenges for you in your environment. We'd love to hear from you about what you need supporting and how any limitations effect you so please feel free to create issues on ",(0,o.kt)("a",{parentName:"p",href:"https://github.com/facebookexperimental/object-introspection"},"GitHub")," or tell us directly on ",(0,o.kt)("a",{parentName:"p",href:"https://matrix.to/#/#object-introspection:matrix.org"},"Matrix")," or ",(0,o.kt)("a",{parentName:"p",href:"irc://irc.oftc.net/#object-introspection"},"IRC"),"."),(0,o.kt)("p",null,"Current known limitations and constraints:"),(0,o.kt)("ul",null,(0,o.kt)("li",null," Statically linked binaries only."),(0,o.kt)("li",null," Split Debug DWARF not currently supported (planned)."),(0,o.kt)("li",null," C style unions not supported."),(0,o.kt)("li",null," Only supported architecture is x86-64."),(0,o.kt)("li",null," Only single probe specifications allowed in an oid invocation"),(0,o.kt)("li",null," Virtual inheritance support"),(0,o.kt)("li",null," Template specialization support"),(0,o.kt)("li",null," Pluggable container support")))}p.isMDXComponent=!0}}]);