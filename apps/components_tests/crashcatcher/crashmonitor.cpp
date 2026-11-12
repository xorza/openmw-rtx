#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <components/crashcatcher/crashanswering.hpp>
#include <components/crashcatcher/crashmonitorarguments.hpp>
#include <components/crashcatcher/crashmonitorlog.hpp>
#include <components/crashcatcher/crashpage.hpp>
#include <components/files/conversion.hpp>
#include <components/platform/process.hpp>
#include <components/testing/util.hpp>

namespace
{
    /// **What the game writes is what its monitor reads**, whatever Crashpad puts around it: every
    /// field back as it went, and nothing of the game's left among Crashpad's arguments, which keep
    /// their order behind `argv[0]`.
    TEST(CrashMonitorArgumentsTest, whatTheGameWritesIsWhatTheMonitorReads)
    {
        Crash::MonitorArguments written;
        written.mClient = 4242;
        written.mIssues = "https://github.com/xorza/openmw-rtx/issues";
        written.mAnswering = Crash::EndAfter{ std::chrono::milliseconds(1500) };

        // The name is Crashpad's annotation, which it hands its handler and the monitor reads.
        std::vector<std::string> line{ "openmw", "--database=/home/x/crashes", "--annotation=product=crash-tests" };
        for (const std::string& argument : written.write())
            line.push_back(argument);
        line.push_back("--initial-client-fd=3");

        std::vector<std::string> handler;
        const Crash::MonitorArguments read = Crash::MonitorArguments::read(line, handler);
        EXPECT_EQ(read.mClient, 4242u);
        EXPECT_EQ(read.mApplication, "crash-tests");
        EXPECT_EQ(read.mIssues, "https://github.com/xorza/openmw-rtx/issues");
        EXPECT_EQ(read.mAnswering, Crash::Answering(Crash::EndAfter{ std::chrono::milliseconds(1500) }));
        EXPECT_EQ(read.mDatabase, std::filesystem::path("/home/x/crashes"));

        const std::vector<std::string> crashpads{ "openmw", "--database=/home/x/crashes",
            "--annotation=product=crash-tests", "--initial-client-fd=3" };
        EXPECT_EQ(handler, crashpads);

        // **Each of the three answerings goes and comes back as itself.**
        for (const Crash::Answering& answering :
            { Crash::Answering(Crash::AskThePlayer{}), Crash::Answering(Crash::AskNobody{}),
                Crash::Answering(Crash::EndAfter{ std::chrono::milliseconds(7) }) })
        {
            written.mAnswering = answering;
            std::vector<std::string> again;
            EXPECT_EQ(Crash::MonitorArguments::read(written.write(), again).mAnswering, answering);
        }
        EXPECT_EQ(written.write().front(), Crash::sMonitorSwitch);
    }

    /// **An End reads as the word `spell` writes, its delay a number to its end**, and nothing else is
    /// one: a harness's text that reads as no End leaves the answering as it was, and a monitor that
    /// reads none asks the player rather than ends. The two words of the builds that counted from
    /// the ask or the report are none.
    TEST(CrashMonitorArgumentsTest, anEndReadsAsTheWordItSpellsAndNothingElse)
    {
        const Crash::EndAfter soon{ std::chrono::milliseconds(1) };
        const Crash::EndAfter late{ std::chrono::milliseconds(5000) };
        EXPECT_EQ(soon.spell(), "end-after-1");
        EXPECT_EQ(late.spell(), "end-after-5000");
        EXPECT_EQ(Crash::EndAfter::read("end-after-1"), soon);
        EXPECT_EQ(Crash::EndAfter::read("end-after-5000"), late);

        for (const std::string_view spelled : { "end-after-", "end-after-12x", "end-after--5", "end-after-+5",
                 "end-after-ask-1", "end-after-report-5000", "end-later-5", "" })
        {
            EXPECT_EQ(Crash::EndAfter::read(spelled), std::nullopt) << spelled;
            std::vector<std::string> handler;
            const std::vector<std::string> line{ "openmw", "--openmw-answering=" + std::string(spelled) };
            EXPECT_EQ(Crash::MonitorArguments::read(line, handler).mAnswering, Crash::Answering(Crash::AskThePlayer{}))
                << spelled;
        }
    }

    /// **A game that names nothing leaves the monitor its defaults**: no game, and the player to answer.
    TEST(CrashMonitorArgumentsTest, theGamesIdReadsAsANumberToItsEnd)
    {
        std::vector<std::string> handler;
        const std::vector<std::string> line{ "openmw" };
        const Crash::MonitorArguments read = Crash::MonitorArguments::read(line, handler);
        EXPECT_EQ(read.mClient, 0u);
        EXPECT_EQ(read.mAnswering, Crash::Answering(Crash::AskThePlayer{}))
            << "a game that names no answer leaves the player to give one";
        EXPECT_EQ(handler, std::vector<std::string>{ "openmw" });

        // **The game's id reads as a number to its end, or is none**, and not the number its first
        // digits make. Which ids a system can signal is the monitor's half to say.
        for (const auto& [spelled, id] : std::initializer_list<std::pair<std::string_view, std::uint32_t>>{
                 { "4242", 4242u },
                 { "4294967295", 4294967295u },
                 { "4294967296", 0u },
                 { "42x", 0u },
                 { "-1", 0u },
                 { "", 0u },
             })
        {
            const std::vector<std::string> named{ "openmw", "--openmw-client=" + std::string(spelled) };
            EXPECT_EQ(Crash::MonitorArguments::read(named, handler).mClient, id) << spelled;
        }
    }

    /// **The page both sides map**, found by the game's id: what one side writes the other reads,
    /// both ways, and a page made fresh starts at nought. An id that made none has none to open.
    TEST(CrashPageTest, theGameAndItsMonitorShareOnePageByTheGamesId)
    {
        // An id no running process has, so the test never meets a real game's page: past every POSIX
        // process id, which is a positive `int`, and odd, where every Windows one is a multiple of four.
        const std::uint32_t id = 0x80000001u + Platform::Process::currentId();

        const Crash::SharedPage game = Crash::SharedPage::create(id);
        ASSERT_NE(game.get(), nullptr);
        EXPECT_EQ(game.get()->mFrames, 0u);
        EXPECT_EQ(game.get()->mHangSeconds, 0u);

        std::atomic_ref(game.get()->mFrames).store(5);
        std::atomic_ref(game.get()->mHangSeconds).store(20);

        const Crash::SharedPage monitor = Crash::SharedPage::open(id);
        ASSERT_NE(monitor.get(), nullptr);
        EXPECT_NE(monitor.get(), game.get()) << "one mapping, not two";
        EXPECT_EQ(std::atomic_ref(monitor.get()->mFrames).load(), 5u);
        EXPECT_EQ(std::atomic_ref(monitor.get()->mHangSeconds).load(), 20u);

        // **The game reads the monitor's answer, not its opening**: the page opened above, and has
        // none yet. Each answer reads back with the monitor's id, and a word past the last answer,
        // which another process wrote, is none.
        EXPECT_EQ(game.readAnswer(), Crash::MonitorAnswer::Unanswered);
        for (const Crash::MonitorAnswer answer : { Crash::MonitorAnswer::DatabaseUnopened,
                 Crash::MonitorAnswer::ReportUnwritable, Crash::MonitorAnswer::Watching })
        {
            monitor.answerGame(answer, 777);
            EXPECT_EQ(game.readAnswer(), answer);
            EXPECT_EQ(game.getMonitor(), 777u);
        }
        std::atomic_ref(monitor.get()->mMonitorAnswer).store(99);
        EXPECT_EQ(game.readAnswer(), Crash::MonitorAnswer::Unanswered);

        // **The end word**: none on a fresh page, then the code the game reached its end with.
        EXPECT_EQ(monitor.getEnd(), std::nullopt);
        game.setEnd(3);
        EXPECT_EQ(monitor.getEnd(), 3);

        // **The log goes over once the game knows it**, which is after the monitor has started:
        // nothing before, the path whole after, letters outside ASCII and a space included, and a
        // path past the page's room is not handed over at all rather than cut.
        EXPECT_EQ(monitor.getLogPath(), "");
        const std::string log = reinterpret_cast<const char*>(u8"C:/Users/Φίλος/My Games/OpenMW/openmw.log");
        EXPECT_TRUE(game.setLogPath(log));
        EXPECT_EQ(monitor.getLogPath(), log);
        EXPECT_FALSE(game.setLogPath(std::string(Crash::sPathCapacity + 1, 'x')));
        EXPECT_EQ(monitor.getLogPath(), log) << "a refused path left the one before";
        EXPECT_FALSE(game.setLogPath("C:/elsewhere.log")) << "a second path written under the first's length";
        EXPECT_EQ(monitor.getLogPath(), log);

        // **And the folder the package goes to**, the same way and apart from the log.
        EXPECT_EQ(monitor.getReportPath(), "");
        const std::string folder = reinterpret_cast<const char*>(u8"D:/Spiele/Φίλος/crashes");
        EXPECT_TRUE(game.setReportPath(folder));
        EXPECT_EQ(monitor.getReportPath(), folder);
        EXPECT_EQ(monitor.getLogPath(), log) << "the folder wrote over the log";
        EXPECT_FALSE(game.setReportPath(std::string(Crash::sPathCapacity + 1, 'x')));
        EXPECT_EQ(monitor.getReportPath(), folder);

        EXPECT_EQ(Crash::SharedPage::open(id + 2).get(), nullptr);
    }

    /// What the monitor adds lands in the game's log, in the order written, stamped as the log stamps a
    /// line, by the time `finish` returns; and **what it wrote before the game named its log waits for
    /// it**, and lands first: a summary of a crash while the configuration was read is not lost.
    TEST(CrashMonitorLogTest, entriesLandInOrderOnceTheGameNamesItsLog)
    {
        // As `theGameAndItsMonitorShareOnePageByTheGamesId` has it, and apart from that test's.
        const std::uint32_t id = 0x80000003u + Platform::Process::currentId();
        const Crash::SharedPage game = Crash::SharedPage::create(id);
        ASSERT_NE(game.get(), nullptr);
        const std::filesystem::path log = TestingOpenMW::outputDirPath("crash-monitor-log.txt");
        const std::filesystem::path fallback = TestingOpenMW::outputDirPath("crash-monitor-own.txt");
        std::filesystem::remove(log);
        std::filesystem::remove(fallback);

        Crash::MonitorLog monitor(game);
        monitor.write({ "before the log" });
        EXPECT_FALSE(std::filesystem::exists(log));
        EXPECT_EQ(monitor.path(), std::filesystem::path());

        ASSERT_TRUE(game.setLogPath(Files::pathToUnicodeString(log)));
        EXPECT_EQ(monitor.path(), log);
        monitor.write({ "Crash: first", "Crash: second" });
        monitor.write({ "Hang: third" });
        monitor.finish(fallback);
        EXPECT_EQ(monitor.path(), log) << "the game named its log, which stays the log";
        EXPECT_FALSE(std::filesystem::exists(fallback));

        std::ifstream file(log);
        std::vector<std::string> said;
        for (std::string line; std::getline(file, line);)
        {
            ASSERT_GT(line.size(), 15u);
            EXPECT_EQ(line.substr(0, 1), "[");
            EXPECT_EQ(line.substr(13, 3), " E]");
            said.push_back(line.substr(17));
        }
        EXPECT_EQ(said, (std::vector<std::string>{ "before the log", "Crash: first", "Crash: second", "Hang: third" }));
    }

    /// **A game that never named its log leaves the monitor's lines in a file of its own**, which
    /// `finish` names and empties first, and which is the log from then on.
    TEST(CrashMonitorLogTest, aGameThatNeverNamedItsLogLeavesTheLinesInTheMonitorsOwn)
    {
        const std::uint32_t id = 0x80000007u + Platform::Process::currentId();
        const Crash::SharedPage game = Crash::SharedPage::create(id);
        ASSERT_NE(game.get(), nullptr);
        const std::filesystem::path fallback = TestingOpenMW::outputDirPath("crash-monitor-fallback.txt");
        std::filesystem::remove(fallback);
        {
            std::ofstream stale(fallback);
            stale << "a session before\n";
        }

        Crash::MonitorLog monitor(game);
        monitor.write({ "Crash: before any log" });
        monitor.finish(fallback);
        EXPECT_EQ(monitor.path(), fallback);

        std::ifstream file(fallback);
        std::vector<std::string> said;
        for (std::string line; std::getline(file, line);)
            said.push_back(line.size() > 17 ? line.substr(17) : line);
        EXPECT_EQ(said, std::vector<std::string>{ "Crash: before any log" });
    }

    /// **The log's end writes what still waits**, with no flush: the monitor's last line names the
    /// package, and a thread that went with the process lost it.
    TEST(CrashMonitorLogTest, whatWaitsIsWrittenAsTheLogEnds)
    {
        const std::uint32_t id = 0x80000005u + Platform::Process::currentId();
        const Crash::SharedPage game = Crash::SharedPage::create(id);
        ASSERT_NE(game.get(), nullptr);
        const std::filesystem::path log = TestingOpenMW::outputDirPath("crash-monitor-log-end.txt");
        std::filesystem::remove(log);
        ASSERT_TRUE(game.setLogPath(Files::pathToUnicodeString(log)));

        {
            Crash::MonitorLog monitor(game);
            monitor.write({ "Crash package: the last line" });
        }

        std::ifstream file(log);
        std::string line;
        ASSERT_TRUE(std::getline(file, line));
        EXPECT_EQ(line.substr(17), "Crash package: the last line");
        EXPECT_FALSE(std::getline(file, line)) << "one line, one write";
    }
}
