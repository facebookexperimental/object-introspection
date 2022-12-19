---
title: A Simple Address Book Example
---

# A simple address book example

Let's start with a very simple C++ application: an address book. This contrived simple piece of code contains everything we need to take you through the basics of using OI. The code itself can be found in the `examples/web/AddrBook` directory in the OI [GitHub repo](https://github.com/facebookexperimental/object-introspection/tree/main/examples/web/AddrBook).

First, build the test application:
```
$ ~/object-introspection/examples/web/AddrBook: make CC=clang++-12
clang++-12 -o addrbook AddrBook.cpp -std=c++20 -g -O3
```

(No need to override the 'CC' make variable if you have `clang++` in your path).

You can see the DWARF data is present in the generated executable:

```
$ ~/object-introspection/examples/web/AddrBook# size -At addrbook | grep "\.debug"
.debug_info           71316         0
.debug_abbrev          2446         0
.debug_line            8971         0
.debug_str            44990         0
.debug_loc            27968         0
.debug_ranges         10240         0
```

Each address book is composed of a single `AddressBook` object which contains zero or more `Contact` objects. Here's how the data and interface definitions look for the two objects:

```C++
class Contact {
public:
  Contact(std::string& f, std::string& l, std::string& n);
private:
  std::string firstName, lastName;
  std::string number;
};

class AddressBook {
public:
  void AddContact(std::string& f, std::string& l, std::string& n);
  void DumpContacts(void);
private:
  int rev;
  std::string Owner;
  std::vector<Contact> Entries;
};
```

OI can introspect objects at specific points in an application:

<ul>
  <li>Function arguments upon entry to a function.</li>
  <li>Function arguments upon return to a function.</li>
  <li>The return value from a function.</li>
  <li>This `this` pointer at entry or return from an object method.</li>
  <li>Global objects.</li>
</ul>

Let's get started by introspecting an object using its `this` pointer.
