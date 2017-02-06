### This document will hopefully evolve in a comprensive guide for developers

Documentation tips:
http://fnch.users.sourceforge.net/doxygen_c.html

Shorts

straph = st
node   = nd
buffer = buf

Comments

Functions:
/**
 * @brief
 *
 * Description
 * @param
 * @param
 * @return
 */


Inline comments:
/* Comment */


Multiline comments:
/*
 This is a comment 
 on multiple lines
*/

Multiline comment after code:

this is some code;  /* Here is a multiline
                       comment following 
                       the code  */

Pointers: void *p and not void* p

Breaking a line:

this_is_a(very, very, very long function, call, when
    I go to a new line I just add one more identation);

Vocabulary:
    execution-edge
    io-edge
    execution flow
    data flow
