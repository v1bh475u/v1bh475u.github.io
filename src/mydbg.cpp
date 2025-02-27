#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/personality.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <fcntl.h>
#include "linenoise.h"

#include "debugger.hpp"
#include "breakpoint.hpp"
#include "regs.hpp"
#include "disassembler.hpp"

void mydbg::debugger::print_backtrace()
{
    int color_grad = 42;
    auto output_frame = [frame_number = 0, &color_grad](auto &&func_name, auto &&func_addr) mutable
    {
        auto color = "\x1B[38;5;" + std::to_string(color_grad++) + "m";
        std::cout << color << "frame #" << frame_number++ << " :0x" << func_addr << ' ' << func_name << "\x1B[0m" << std::endl;
    };
    if (debugger::DI == debug_info::DWARF)
    {

        auto current_func = get_function_from_pc(offset_load_address(get_pc()));
        output_frame(dwarf::at_name(current_func), dwarf::at_low_pc(current_func));
        auto frame_pointer = get_register_value(m_pid, reg::rbp);
        auto return_address = read_memory(frame_pointer + 8);
        while (dwarf::at_name(current_func) != "main")
        {
            current_func = get_function_from_pc(offset_load_address(return_address));
            output_frame(dwarf::at_name(current_func), dwarf::at_low_pc(current_func));
            frame_pointer = read_memory(frame_pointer);
            return_address = read_memory(frame_pointer + 8);
        }
    }
    else
    {
        auto current_func = get_function_from_pc_symtab(offset_load_address(get_pc()));
        output_frame(current_func.name, current_func.address);
        auto frame_pointer = get_register_value(m_pid, reg::rbp);
        auto return_address = read_memory(frame_pointer + 8);
        while (current_func.name != "main")
        {
            current_func = get_function_from_pc_symtab(offset_load_address(return_address));
            output_frame(current_func.name, current_func.address);
            frame_pointer = read_memory(frame_pointer);
            return_address = read_memory(frame_pointer + 8);
        }
    }
}

mydbg::function_info mydbg::debugger::get_function_from_pc_symtab(uint64_t pc)
{
    auto eHdr = debugger::get_elfhdr();
    function_info info;

    for (int i = 0; i < eHdr.shnum; i++)
    {
        elf::section sec = get_section(i);
        if (sec.get_hdr().type == elf::sht::symtab)
        {
            for (const auto &sym : sec.as_symtab())
            {
                auto sym_value = sym.get_data().value;
                auto sym_size = sym.get_data().size;
                if (pc >= sym_value && pc < sym_value + sym_size)
                {
                    info.name = sym.get_name();
                    info.address = sym_value;
                    return info;
                }
            }
        }
    }
    info.name = "unkown";
    info.address = pc;
    return info;
}

void mydbg::debugger::handle_sigtrap(siginfo_t info)
{
    switch (info.si_code)
    {
    case SI_KERNEL:
    case TRAP_BRKPT:
    {
        set_pc(get_pc() - 1);
        std::cout << "Hit breakpoint at 0x" << std::hex << get_pc() << std::endl;
        if (debugger::DI == debug_info::DWARF)
        {
            auto offset_pc = offset_load_address(get_pc());
            auto line_entry = get_line_entry_from_pc(offset_pc);
            print_source(line_entry->file->path, line_entry->line);
        }
        return;
    }
    case TRAP_TRACE:
        return;
    default:
        std::cout << "Unknown SIGTRAP code " << info.si_code << std::endl;
        return;
    }
}
siginfo_t mydbg::debugger::get_signal_info()
{
    siginfo_t info;
    ptrace(PTRACE_GETSIGINFO, m_pid, nullptr, &info);
    return info;
}

uint64_t mydbg::debugger::offset_load_address(uint64_t addr)
{
    return addr - m_load_addr;
}

void mydbg::debugger::initialise_load_address()
{
    if (m_elf.get_hdr().type == elf::et::dyn)
    {
        std::ifstream map("/proc/" + std::to_string(m_pid) + "/maps");
        std::string addr;
        std::getline(map, addr, '-');
        m_load_addr = std::stol(addr, 0, 16);
    }
}
void mydbg::debugger::print_source(const std::string &file_name, unsigned line, unsigned n_lines_context)
{
    std::ifstream file{file_name};

    auto startline = line <= n_lines_context ? 1 : line - n_lines_context;
    auto endline = line + n_lines_context + (line < n_lines_context ? n_lines_context - line : 0) + 1;
    char c{};
    auto current_line = 1u;
    while (current_line != startline && file.get(c))
    {
        if (c == '\n')
        {
            ++current_line;
        }
    }

    std::cout << (current_line == line ? "> " : "  ");

    while (current_line != endline && file.get(c))
    {
        std::cout << c;
        if (c == '\n')
        {
            current_line++;
            std::cout << (current_line == line ? "> " : "  ");
        }
    }
    std::cout << std::endl;
}

dwarf::die mydbg::debugger::get_function_from_pc(uint64_t pc)
{
    for (auto &cu : m_dwarf.compilation_units())
    {
        if (die_pc_range(cu.root()).contains(pc))
        {
            for (const auto &die : cu.root())
            {
                if (die.tag == dwarf::DW_TAG::subprogram)
                {
                    if (die.has(dwarf::DW_AT::low_pc))
                    {
                        if (die_pc_range(die).contains(pc))
                        {
                            return die;
                        }
                    }
                }
            }
        }
    }

    throw std::out_of_range{"Cannot find function"};
}

dwarf::line_table::iterator mydbg::debugger::get_line_entry_from_pc(uint64_t pc)
{
    for (auto &cu : m_dwarf.compilation_units())
    {
        if (die_pc_range(cu.root()).contains(pc))
        {
            auto &lt = cu.get_line_table();
            auto it = lt.find_address(pc);
            if (it == lt.end())
            {
                throw std::out_of_range{"Cannot find line entry"};
            }
            else
            {
                return it;
            }
        }
    }

    throw std::out_of_range{"Cannot find line entry"};
}

std::vector<std::string> split(const std::string &s, char delimiter)
{
    std::vector<std::string> out{};
    std::stringstream ss{s};
    std::string item;

    while (std::getline(ss, item, delimiter))
    {
        out.push_back(item);
    }

    return out;
}

bool is_prefix(const std::string &s, const std::string &of)
{
    if (s.size() > of.size())
        return false;
    return std::equal(s.begin(), s.end(), of.begin());
}

void mydbg::debugger::run()
{
    wait_for_signal();
    initialise_load_address();
    char *line = nullptr;
    while ((line = linenoise("\x1B[1m\x1B[92mmydbg> \x1B[0m")) != nullptr)
    {
        // dump_registers();
        handle_command(line);
        linenoiseHistoryAdd(line);
        linenoiseFree(line);
    }
}

void mydbg::debugger::handle_command(const std::string &line)
{
    // auto offset_pc = offset_load_address(get_pc());
    // auto line_entry = get_line_entry_from_pc(offset_pc);
    // print_source(line_entry->file->path, line_entry->line);
    auto args = split(line, ' ');
    auto command = args[0];
    if (is_prefix(command, "c"))
    {
        continue_execution();
    }
    else if (is_prefix(command, "b"))
    {
        std::string addr{args[1]};
        set_breakpoint_at(std::stol(addr, 0, 16));
    }
    else if (is_prefix(command, "regs"))
    {
        if (is_prefix(args[1], "dump"))
        {
            dump_registers();
        }
        else if (is_prefix(args[1], "r"))
        {
            if (args.size() != 3)
            {
                std::cerr << "\x1B[1m\x1B[91mInvalid number of arguments\x1B[38;5;28m\nPlease provide a register name in the format like rip,rax\x1B[0m\n";
                return;
            }
            std::cout << "\x1B[38;5;27m" << args[2] << "\x1B[0m:\t0x" << std::hex << get_register_value(m_pid, get_register_from_name(args[2])) << std::endl;
        }
        else if (is_prefix(args[1], "w"))
        {
            std::string val{args[3], 2};
            set_register_value(m_pid, get_register_from_name(args[2]), std::stol(val, 0, 16));
        }
        else
        {
            std::cerr << "\x1B[1m\x1B[91mUnknown command\x1B[0m\n";
        }
    }
    else if (is_prefix(command, "mem"))
    {
        std::string addr{args[2], 2};
        if (is_prefix(args[1], "r"))
        {
            std::cout << std::hex << read_memory(std::stol(addr, 0, 16)) << std::endl;
        }
        else if (is_prefix(args[1], "w"))
        {
            std::string val{args[3], 2};
            write_memory(std::stol(addr, 0, 16), std::stol(val, 0, 16));
        }
    }
    else if (is_prefix(command, "so"))
    {
        step_over_breakpoint();
    }
    else if (is_prefix(command, "bt"))
    {
        print_backtrace();
    }
    else if (is_prefix(command, "disass"))
    {
        disassember di{m_prog_name};
        if(args.size() != 2)
        {
            std::cerr << "\x1B[1m\x1B[91mInvalid number of arguments\x1B[38;5;28m\nPlease provide a function name\x1B[0m\n";
            return;
        }
        func_data func = di.get_func(args[1]);
        di.disassemble(func.func_code, func.code_size, func.address);
    }
    else if (is_prefix(command, "q"))
    {
        kill(m_pid, SIGKILL);
        std::cout << "\x1B[1m\x1B[38;5;196mExiting debugger\x1B[0m\n";
        exit(0);
    }
    else if (is_prefix(command, "si"))
    {
        step_over_breakpoint();
        // ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, nullptr);
        // wait_for_signal();
    }
    else if (is_prefix(command, "help"))
    {
        std::cout << "\x1B[1m\x1B[38;5;mCommands:\n"
                  << "b <address> - Set breakpoint at address\n"
                  << "c - Continue execution\n"
                  << "regs dump - Dump registers\n"
                  << "regs r <register> - Read register value\n"
                  << "regs w <register> <value> - Write register value\n"
                  << "mem r <address> - Read memory at address\n"
                  << "mem w <address> <value> - Write memory at address\n"
                  << "disass <function> - Disassemble function\n"
                  << "so - Step over breakpoint\n"
                  << "si - Step instruction\n"
                  << "bt             - shows call stack\n"
                  << "q - Quit debugger\n"
                  << "help - Show this help message\n\x1B[0m";
    }
    else if (is_prefix(command, "cmd"))
    {
        std::string cmd = line.substr(4);
        system(cmd.c_str());
    }
    else
    {
        std::cerr << "\x1B[1m\x1B[91mUnknown command\x1B[0m\n";
    }
}

uint64_t mydbg::debugger::read_memory(uint64_t address)
{
    return ptrace(PTRACE_PEEKDATA, m_pid, address, nullptr);
}

void mydbg::debugger::write_memory(uint64_t address, uint64_t value)
{
    ptrace(PTRACE_POKEDATA, m_pid, address, value);
}

void mydbg::debugger::continue_execution()
{
    step_over_breakpoint();
    ptrace(PTRACE_CONT, m_pid, nullptr, nullptr);
    wait_for_signal();
}

void mydbg::debugger::set_breakpoint_at(uint64_t addr)
{
    intptr_t m_address = static_cast<intptr_t>(m_load_addr + addr);
    std::cout << "Set breakpoint at address \x1B[38;5;196m0x" << std::hex << m_address << "\x1B[0m" << std::endl;
    breakpoint bp{m_pid, m_address};
    bp.enable();
    m_breakpoints[m_address] = bp;
    for (auto const &bp : m_breakpoints)
    {
        std::cout << "Breakpoint at address \x1B[38;5;196m0x" << std::hex << bp.first << "\x1B[0m" << std::endl;
    }
}

void mydbg::debugger::dump_registers()
{
    for (const auto &rd : g_register_descriptors)
    {
        if (rd.name != "orig_rax" && rd.name != "cs" && rd.name != "rflags" && rd.name != "ss" && rd.name != "fs_base" && rd.name != "eflags" && rd.name != "gs_base" && rd.name != "ds" && rd.name != "es" && rd.name != "fs" && rd.name != "gs")
            std::cout << "\u001b[33m" << rd.name << "\u001b[0m\t: 0x" << std::setfill('0') << std::setw(16) << std::hex << get_register_value(m_pid, rd.r) << std::endl;
    }
}

uint64_t mydbg::debugger::get_pc()
{
    return get_register_value(m_pid, reg::rip);
}

void mydbg::debugger::wait_for_signal()
{
    int wait_status;
    auto options = 0;
    waitpid(m_pid, &wait_status, options);

    auto siginfo = get_signal_info();

    switch (siginfo.si_signo)
    {
    case SIGTRAP:
        handle_sigtrap(siginfo);
        break;
    case SIGSEGV:
        std::cout << "Yay, segfault. Reason: " << siginfo.si_code << std::endl;
        break;
    default:
        std::cout << "Got signal " << strsignal(siginfo.si_signo) << std::endl;
    }
}

void mydbg::debugger::set_pc(uint64_t pc)
{
    set_register_value(m_pid, reg::rip, pc);
}

void mydbg::debugger::step_over_breakpoint()
{
    if (m_breakpoints.count(get_pc()))
    {
        auto &bp = m_breakpoints[get_pc()];
        if (bp.is_enabled())
        {
            bp.disable();
            ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, nullptr);
            wait_for_signal();
            bp.enable();
        }
        else
        {
            ptrace(PTRACE_SINGLESTEP, nullptr, nullptr);
            wait_for_signal();
        }
    }
}

void execute_debugee(const std::string &prog_name)
{
    if (ptrace(PTRACE_TRACEME, 0, 0, 0) < 0)
    {

        std::cerr
            << "Ptrace unsuccessful\n";
        return;
    }
    execl(prog_name.c_str(), prog_name.c_str(), nullptr);
}

int main(int argc, char *argv[])
{
    // ptrace(ptrace_request, pid, addr, data)
    if (argc < 2)
    {
        std::cerr << "Usage: ./debugger <program>";
        return EXIT_FAILURE;
    }
    auto prog = argv[1];
    auto pid = fork();
    if (pid == 0)
    {
        personality(ADDR_NO_RANDOMIZE);
        execute_debugee(prog);
    }
    else if (pid >= 1)
    {
        std::cout << "Started debugging process " << pid << '\n';
        std::string command = "cat /proc/" + std::to_string(pid) + "/maps| sed -n '1{s/^\\([^ -]*\\).*/\\1/p}'";
        FILE *pipe = popen(command.c_str(), "r");
        if (!pipe)
        {
            std::cerr << "Error: Failed to run the command\n";
            return EXIT_FAILURE;
        }
        char buffer[256];
        std::string addr;
        if (fgets(buffer, 256, pipe) != nullptr)
        {
            addr = buffer;
            addr.erase(std::remove(addr.begin(), addr.end(), '\n'), addr.end());
        }
        pclose(pipe);
        std::cout << "Address loaded:" << addr << std::endl;
        mydbg::debugger dbg{prog, pid};
        dbg.run();
    }
}