#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

class Contact {
 public:
  Contact(std::string& f, std::string& l, std::string& n) {
    firstName = f;
    lastName = l;
    number = n;
  };
  std::string getFirstName() {
    return firstName;
  };
  std::string getLastName() {
    return lastName;
  };
  std::string getNumber() {
    return number;
  };

 private:
  std::string firstName, lastName;
  std::string number;
};

class AddressBook {
 public:
  void AddContact(std::string& f, std::string& l, std::string& n) {
    Entries.insert(Entries.begin(), Contact(f, l, n));
  };

  void DumpContacts(void) {
    int sz = 0;
    std::cout << "number of Entries: " << Entries.size() << std::endl;
    for (auto contact : Entries) {
      std::string firstName = contact.getFirstName();
      std::string lastName = contact.getLastName();
      std::string number = contact.getNumber();

      sz +=
          sizeof(contact) + firstName.size() + lastName.size() + number.size();

      std::cout << "sizeof contact = " << sizeof(contact) << " sizeof fname "
                << sizeof(firstName) << " sizeof lname: " << sizeof(lastName)
                << " sizeof number: " << sizeof(number)
                << " size fname:  " << firstName.size()
                << " size lname: " << lastName.size()
                << " size number: " << number.size() << std::endl;
    }
    std::cout << "Total size = " << sz << " bytes\n\n";
  };

 private:
  int rev;
  std::string Owner;
  std::vector<Contact> Entries;
};

/*
 * Borrowed from
 * https://stackoverflow.com/questions/440133/how-do-i-create-a-random-alpha-numeric-string-in-c
 * .
 */
std::string random_string(size_t length) {
  auto randchar = []() -> char {
    const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    const size_t maxIndex = (sizeof(charset) - 1);
    return charset[rand() % maxIndex];
  };

  std::string str(length, 0);
  std::generate_n(str.begin(), length, randchar);
  return str;
}

AddressBook globalAddrBook;

int main(int argc, char* argv[]) {
  AddressBook A;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> distr(1, 100);

  while (1) {
    std::string fname = random_string(distr(gen));
    std::string sname = random_string(distr(gen));
    std::string number = random_string(distr(gen));

    std::this_thread::sleep_for(std::chrono::seconds(1));
    A.AddContact(fname, sname, number);
    A.DumpContacts();
  }
}
