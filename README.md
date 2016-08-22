Arctic Core 
============

https://www.arcticcoin.org

Copyright (c) 2009-2015 Bitcoin Core Developers
Copyright (c) 2015-2016 Arctic Core Developers


What is Arctic?
----------------

«ArcticCoin» the first national cryptocurrency,
created to improve the welfare of citizens
and support to the national economy.
ArcticCoin is a cryptocurrency and open source. 
With the help of the ArcticCoin, you will be able to make payments and transfers instantly, with no restrictions anywhere in the world, 
while maintaining anonymity and security, as in the calculations in cash.

For more information, about «ArcticCoin», see https://www.arcticcoin.org

«ArcticCoin» первая национальная криптовалюта,
созданная для улучшения благосостояния граждан
и помощи национальной экономике.
ArcticCoin - это криптовалюта с открытым исходным кодом. 
С помощью ArcticCoin вы сможете производить платежи и переводы мгновенно, без ограничений в любую точку мира, 
при этом сохраняя анонимность и безопасность, как при расчётах наличными деньгами.

Вся дополнительная информация о «ArcticCoin» на сайте https://www.arcticcoin.org

License
-------

Arctic Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see http://opensource.org/licenses/MIT.

Development Process
-------------------

The `master` branch is meant to be stable. Development is normally done in separate branches.
[Tags](https://github.com/ArcticCore/arcticcoin/tags) are created to indicate new official,
stable release versions of Arctic Core.

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md).


Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

### Automated Testing

Developers are strongly encouraged to write unit tests for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run (assuming they weren't disabled in configure) with: `make check`

Every pull request is built for both Windows and Linux on a dedicated server,
and unit and sanity tests are automatically run. The binaries produced may be
used for manual QA testing — a link to them will appear in a comment on the
pull request posted by [ArcticPullTester](https://github.com/ArcticCore/PullTester). See https://github.com/TheBlueMatt/test-scripts
for the build/test scripts. ***TODO***

### Manual Quality Assurance (QA) Testing

Large changes should have a test plan, and should be tested by somebody other
than the developer who wrote the code.
See https://github.com/ArcticCore/QA/ for how to create a test plan. ***TODO***

Translations
------------

Changes to translations as well as new translations can be submitted to
[Bitcoin Core's Transifex page](https://www.transifex.com/projects/p/arcticcoin/).

Translations are periodically pulled from Transifex and merged into the git repository. See the
[translation process](doc/translation_process.md) for details on how this works.

**Important**: We do not accept translation changes as GitHub pull requests because the next
pull from Transifex would automatically overwrite them again.

Translators should also subscribe to the [mailing list](https://groups.google.com/forum/#!forum/arcticcoin-translators). ***TODO***

Development tips and tricks
---------------------------

**compiling for debugging**

Run configure with the --enable-debug option, then make. Or run configure with
CXXFLAGS="-g -ggdb -O0" or whatever debug flags you need.

**debug.log**

If the code is behaving strangely, take a look in the debug.log file in the data directory;
error and debugging messages are written there.

The -debug=... command-line option controls debugging; running with just -debug will turn
on all categories (and give you a very large debug.log file).

The Qt code routes qDebug() output to debug.log under category "qt": run with -debug=qt
to see it.

**testnet and regtest modes**

Run with the -testnet option to run with "play arcticcoin" on the test network, if you
are testing multi-machine code that needs to operate across the internet.

If you are testing something that can run on one machine, run with the -regtest option.
In regression test mode, blocks can be created on-demand; see qa/rpc-tests/ for tests
that run in -regtest mode.

**DEBUG_LOCKORDER**

Arctic Core is a multithreaded application, and deadlocks or other multithreading bugs
can be very difficult to track down. Compiling with -DDEBUG_LOCKORDER (configure
CXXFLAGS="-DDEBUG_LOCKORDER -g") inserts run-time checks to keep track of which locks
are held, and adds warnings to the debug.log file if inconsistencies are detected.
