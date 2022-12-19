---
title: Function Arguments
---

# Introspecting Function Arguments

We'll now look at a simple example of introspecting objects upon entry to a function. Adding a contact to the address book takes 3 string objects passed by reference:

```
  void AddContact(std::string& f, std::string& l, std::string& n);
```

We need the symbol name to form the probe specification:

```
$ ~/object-introspection/examples/web/AddrBook# readelf -sW addrbook | grep AddContext
    76: 0000000000401980   221 FUNC    WEAK   DEFAULT   15 _ZN11AddressBook10AddContactERNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES6_S6_
```

So the probe specification for introspecting the first and third argument (FirstName and Number) passed to `AddContact()` is:

```
$ ~/object-introspection/examples/web/AddrBook# cat /tmp/addrentry.oid
entry:_ZN11AddressBook10AddContactERNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEES6_S6_:arg0,arg2
```

A few small things to note:

<ul>
  <li>Instead of specifying the probe on the command line with the `-S` option we've put it into a file as longer probe specifications become a bit unwieldy in the command line for some of us!</li>
  <li>Arguments to functions are referenced by `argX` where `X` begins at 0 for the first argument.</li>
  <li>One or more arguments can be specified in the probe specification.</li>
</ul>

```
$ ~/object-introspection# jq . oid_out.json
[
  {
    "name": "f",
    "typePath": "f",
    "typeName": "string",
    "isTypedef": true,
    "staticSize": 32,
    "dynamicSize": 0,
    "members": [
      {
        "name": "",
        "typePath": "",
        "typeName": "basic_string<char, std::char_traits<char>, std::allocator<char> >",
        "isTypedef": false,
        "staticSize": 32,
        "dynamicSize": 0,
        "length": 14,
        "capacity": 15,
        "elementStaticSize": 1
      }
    ]
  },
  {
    "name": "n",
    "typePath": "n",
    "typeName": "string",
    "isTypedef": true,
    "staticSize": 32,
    "dynamicSize": 23,
    "members": [
      {
        "name": "",
        "typePath": "",
        "typeName": "basic_string<char, std::char_traits<char>, std::allocator<char> >",
        "isTypedef": false,
        "staticSize": 32,
        "dynamicSize": 23,
        "length": 23,
        "capacity": 23,
        "elementStaticSize": 1
      }
    ]
  }
]
```

The above shows us that the first argument, f, is 14 bytes in length and fits within the SSO static buffer while the third argument, n, is 23 characters long and there occupies dynamically allocated memory external the the string object.
