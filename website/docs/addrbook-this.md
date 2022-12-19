---
title: this pointers
---

We specify exactly which function and arguments are to be introspected through a ***probe specification*** (terminology borrowed from [DTrace](https://en.wikipedia.org/wiki/DTrace)). It's simply a colon delimited tuple that specifies exactly what object we are interested in and where in the code we want to observe it. For example, to introspect the `AddressBook` object we can measure it at the entry to its `DumpContacts()` method and the specification would be:

```
    entry:_ZN11AddressBook12DumpContactsEv:this
```

There are a few points to note here:

<ul>
  <li>We are using the <code>AddressBook::DumpContacts</code> method. Note that we specify the function name using the mangled C++ name which can be found using <code>readelf -sW /path/to/binary | grep symbol</code>.</li>
  <li>We are introspecting the object itself through the <code>this</code> specifier.</li>
  <li>We are introspecting on entry to the method but we could have chosen to introspect the object the object at the return point from the <code>AddressBook::DumpContacts()</code> method which would be useful if state had been altered during execution of he method.</li>
</ul>

We use the `oid` debugger to capture the object itself from the address book application. Every second a contact is added to the address book with the `AddContact()` method and the address book is dumped with the `DumpContacts()` method. After running for a minute or so let's capture the Address book object:

```
$ build/oid -S 'entry:_ZN11AddressBook12DumpContactsEv:this' -p `pgrep addrbook` -c build/oid-cfg.toml -J
Attached to pid 4039830
SUCCESS
```

The `-J` flag instructed `oid` to dump the captured objects introspection data in a JSON file:

```
$ ~/object-introspection# ls -l oid_out.json
-rw-r--r-- 1 root root 78975 Dec 16 20:35 oid_out.json
```

This is a simple object but the resulting JSON becomes quite large when more than a handful of contacts have been added. Let's examine some sections to see what we can glean about the object that has been captured:

```
$ jq . oid_out.json
[
  {
    "name": "this",
    "typePath": "this",
    "typeName": "AddressBook",
    "isTypedef": false,
    "staticSize": 64,
    "dynamicSize": 23185,
    "paddingSavingsSize": 4,
    "members": [
```

The root type is an <code>AddressBook</code> object as we'd expect and it has a static footprint of 64 bytes with its data members in total having a dynamic memory footprint of 23185 bytes.

The first member is simply an integer for the <code>rev</code> data member which has a 4 byte static memory footprint on this platform.

```
    "members": [
      {
        "name": "rev",
        "typePath": "rev",
        "typeName": "int",
        "isTypedef": false,
        "staticSize": 4,
        "dynamicSize": 0
      },
```

Next we have a <code>std::string</code> object for the top level <code>Owner</code> member. Note how this is expanded down to the base types which show the string has a static footprint of 32 bytes but we can see that it is empty as it has a <code>dynamicSize</code> of 0. The <code>capacity</code> of 15 bytes shows the Short String optimization buffer in a libstdc++ <code>std::string</code> object.

```
      {
        "name": "Owner",
        "typePath": "Owner",
        "typeName": "string",
        "isTypedef": true,
        "staticSize": 32,
        "dynamicSize": 0,
        "members": [
          {
            "name": "",
            "typePath": "",
            "typeName": "basic_string<char, std::char_traits<char>, std::alloca
tor<char> >",
            "isTypedef": false,
            "staticSize": 32,
            "dynamicSize": 0,
            "length": 0,
            "capacity": 15,
            "elementStaticSize": 1
          }
        ]
      },
```

The <code>Entries</code> member gets a bit more interesting as it introduces usage of a C++ container class, the <code>std::vector</code>. More accurately, it is a vector of <code>Contact</code> objects as can be seen in the <code>typeName</code> JSON member for the root of the <code>Entries</code> object shown below. Note that the vector currently has an allocated <code>capacity</code> of 128 <code>Contact</code> elements of which 71 are actual <code>Contact</code> objects. The 23185 bytes of dynamically allocated objects in the vector is composed of the 71 x 96 byte <code>Contact</code> objects plus whatever memory that has been dynamically allocated for string content in those objects:
```
      {
        "name": "Entries",
        "typePath": "Entries",
        "typeName": "vector<Contact, std::allocator<Contact> >",
        "isTypedef": false,
        "staticSize": 24,
        "dynamicSize": 23185,
        "pointer": 140720550450568,
        "length": 71,
        "capacity": 128,
        "elementStaticSize": 96,
```

For the sake of brevity let's just inspect the first `Contact` object shown below. It's static footprint at 96 bytes is the three strings at 32 bytes each. We can see that the first and third string have strings larger than the 15 bytes allocated for the short string optimization buffer and are therefore allocated in dynamic memory that is external to the string objects themselves. The second string has no dynamic memory allocated as the 14 byte character sequence it is housing fits within the 15 byte pre-allocated buffer:
```

        "members": [
          {
            "name": "",
            "typePath": "Contact[]",
            "typeName": "Contact",
            "isTypedef": false,
            "staticSize": 96,
            "dynamicSize": 65,
            "members": [
              {
                "name": "firstName",
                "typePath": "firstName",
                "typeName": "string",
                "isTypedef": true,
                "staticSize": 32,
                "dynamicSize": 35,
                "members": [
                  {
                    "name": "",
                    "typePath": "",
                    "typeName": "basic_string<char, std::char_traits<char>, std::allocator<char> >",
                    "isTypedef": false,
                    "staticSize": 32,
                    "dynamicSize": 35,
                    "length": 35,
                    "capacity": 35,
                    "elementStaticSize": 1
                  }
                ]
              },
              {
                "name": "lastName",
                "typePath": "lastName",
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
                "name": "number",
                "typePath": "number",
                "typeName": "string",
                "isTypedef": true,
                "staticSize": 32,
                "dynamicSize": 30,
                "members": [
                  {
                    "name": "",
                    "typePath": "",
                    "typeName": "basic_string<char, std::char_traits<char>, std::allocator<char> >",
                    "isTypedef": false,
                    "staticSize": 32,
                    "dynamicSize": 30,
                    "length": 18,
                    "capacity": 30,
                    "elementStaticSize": 1
                  }
                ]
              }
```

In total we have 71 `Contact` objects introspected here and each can be analyzed as we have done above:
```
$ ~/object-introspection# jq . /tmp/oit.oit.json.fmt | grep -c "\"typeName\": \"Contact\""
71
```
