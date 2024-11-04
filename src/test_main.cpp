/*
 * Created: 2024/9/2
 * Author:  hineven
 * See LICENSE for licensing.
 */
#include <cstdio>
#include <memory>
class SomeClass {
    std::shared_ptr<int> value;
public:
    std::shared_ptr<int> someMemberFunction () {
        return value;
    }
    std::shared_ptr<int> & someMemberFunctionReturningARef () {
        return value;
    }
};

int main () {
    SomeClass someClass;
    // Correct, value = rvalue
    auto value = someClass.someMemberFunction();
    // Incorrect, lvref = rvalue
    auto &lvref = someClass.someMemberFunction();
    // Correct, lvref = lvref
    auto &lvref2 = someClass.someMemberFunctionReturningARef();
    return 0;
}