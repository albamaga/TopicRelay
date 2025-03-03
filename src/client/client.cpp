#include <iostream>
#include <thread>
#include <vector>
#include <unordered_map>
#include <functional>
#include <sstream>
#include <boost/asio.hpp>
#include <unistd.h> // For getpid() on Linux/macOS
#include <sys/types.h>
#include "argparse/argparse.hpp"

using boost::asio::ip::tcp;
using CommandHandler = std::function<void(std::vector<std::string>)>;

// Command handler map
std::unordered_map<std::string, CommandHandler> command_handlers;

// Mutexes
std::mutex socket_mutex;

tcp::socket *global_socket = nullptr;
bool connected = false;

void process_command(const std::string &input);

void listener_message_receive(tcp::socket &socket);

void setup_command_handlers();
void handle_connect(std::vector<std::string> args);
void handle_disconnect(std::vector<std::string>);
void handle_publish(std::vector<std::string> args);
void handle_subscribe(std::vector<std::string> args);
void handle_unsubscribe(std::vector<std::string> args);

void send_command(const std::string &command);
void cleanup_connection();

/**
 * @brief Client application
 *
 * @param argc Argument count
 * @param argv Arguments
 * @return int Returns a status to the caller
 */
int main(int argc, char *argv[])
{
    argparse::ArgumentParser program("client", "1.0.1-nightly");

    program.add_argument("-s", "--server")
        .default_value(std::string("127.0.0.1"))
        .help("Server IP address");

    program.add_argument("-p", "--port")
        .default_value(std::string(""))
        .help("Server port");

    program.add_argument("-n", "--name")
        .default_value(std::string(""))
        .help("Client name");

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err)
    {
        std::cerr << "Argument parsing error: " << err.what() << "\n";
        std::cout << program;
        return 1;
    }

    setup_command_handlers();

    std::string server_ip = program.get<std::string>("--server");
    std::string port = program.get<std::string>("--port");
    std::string client_name = program.get<std::string>("--name");

    if (!port.empty() && !client_name.empty())
    {
        command_handlers["CONNECT"]({server_ip, port, client_name});
    }
    else
    {
        std::cout << "No connection established.\n"
                  << "Use:\n"
                  << "\tCONNECT <serverIP> <serverPort> <clientName>\n"
                  << "\tCONNECT <serverPort> <clientName>\n";
    }

    std::string input;
    while (true)
    {
        std::getline(std::cin, input);
        if (input == "exit")
            break;
        process_command(input);
    }

    command_handlers["DISCONNECT"]({});
    std::cout << "Exiting client...\n";
    return 0;
}

/**
 * @brief Function to process user input and execute corresponding command
 *
 * @param input
 */
void process_command(const std::string &input)
{
    std::istringstream ss(input);
    std::string command;
    std::vector<std::string> args;
    ss >> command;

    std::string arg;
    while (ss >> arg)
    {
        args.push_back(arg);
    }

    auto it = command_handlers.find(command);
    if (it != command_handlers.end())
    {
        it->second(args);
    }
    else
    {
        std::cout << "Invalid command! Use:\n"
                  << "  CONNECT <serverIP> <serverPort> <clientName>\n"
                  << "  CONNECT <serverPort> <clientName>\n"
                  << "  DISCONNECT\n"
                  << "  PUBLISH <topic> <data>\n"
                  << "  SUBSCRIBE <topic>\n"
                  << "  UNSUBSCRIBE <topic>\n";
    }
}

/**
 * @brief Function to receive messages
 *
 * @param socket TCP Socket
 */
void listener_message_receive(tcp::socket &socket)
{
    try
    {
        char data[1024];
        while (true)
        {
            boost::system::error_code error;
            size_t length = socket.read_some(boost::asio::buffer(data), error);

            if (error == boost::asio::error::eof)
            {
                std::cout << "[DISCONNECT] Server closed the connection.\n";
                cleanup_connection();
                break;
            }
            else if (error)
            {
                throw boost::system::system_error(error);
            }

            std::string received_msg(data, length);
            std::cout << received_msg << std::endl;
        }
    }
    catch (std::exception &e)
    {
        cleanup_connection();
    }
}

/**
 * @brief Setup of command handlers
 *
 */
void setup_command_handlers()
{
    command_handlers["CONNECT"] = handle_connect;
    command_handlers["DISCONNECT"] = handle_disconnect;
    command_handlers["PUBLISH"] = handle_publish;
    command_handlers["SUBSCRIBE"] = handle_subscribe;
    command_handlers["UNSUBSCRIBE"] = handle_unsubscribe;
}

/**
 * @brief Connect command Handler
 *
 * @param args Command arguments, IP, PORT, NAME
 */
void handle_connect(std::vector<std::string> args)
{
    if (args.size() < 2 || args.size() > 3)
    {
        std::cout << "Invalid CONNECT command. Use:\n"
                  << "  CONNECT <serverIP> <serverPort> <clientName>\n"
                  << "  CONNECT <serverPort> <clientName>\n";
        return;
    }

    if (connected)
    {
        std::cout << "[WARNING] Already conneedted";
        return;
    }

    std::string server_ip = (args.size() == 3) ? args[0] : "127.0.0.1";
    std::string port = (args.size() == 3) ? args[1] : args[0];
    std::string client_name = (args.size() == 3) ? args[2] : args[1];

    // Get the process ID (PID)
    pid_t pid = getpid();

    cleanup_connection(); // Ensure previous connection is cleaned up

    try
    {
        boost::asio::io_context *io_context = new boost::asio::io_context();
        tcp::socket *socket = new tcp::socket(*io_context);
        tcp::resolver resolver(*io_context);

        auto endpoints = resolver.resolve(server_ip, port);
        boost::asio::connect(*socket, endpoints);

        {
            std::lock_guard<std::mutex> lock(socket_mutex);
            global_socket = socket;
            connected = true;
        }

        // Send CONNECT command with PID
        send_command("CONNECT " + port + " " + client_name + " " + std::to_string(pid));

        // Log successful connection
        std::cout << "[CONNECT] (success) [" << client_name << " (" << pid << ") " << server_ip << " " << port << "]\n";

        // Start receiving thread
        std::thread receiver(listener_message_receive, std::ref(*socket));
        receiver.detach();
    }
    catch (std::exception &e)
    {
        std::cerr << "[CONNECT] (failed) [" << client_name << " (" << pid << ") " << server_ip << " " << port << "] (" << e.what() << ")\n";
    }
}

/**
 * @brief Disconnect command Handler
 *
 */
void handle_disconnect(std::vector<std::string>)
{
    send_command("DISCONNECT");
    cleanup_connection();
    std::cout << "[DISCONNECT] Client manually disconnected.\n";
}

/**
 * @brief Publish command Handler
 *
 * @param args Topic and Data to be sent to the server
 */
void handle_publish(std::vector<std::string> args)
{
    if (args.size() < 2)
    {
        std::cout << "Invalid PUBLISH command. Use:\n  PUBLISH <topic> <data>\n";
        return;
    }

    std::ostringstream oss;
    oss << "PUBLISH " << args[0] << " ";
    for (size_t i = 1; i < args.size(); ++i)
    {
        oss << args[i] << " ";
    }

    send_command(oss.str());
}

/**
 * @brief Subscribe command Handler
 *
 * @param args Topics to subscribe to
 */
void handle_subscribe(std::vector<std::string> args)
{
    if (args.size() != 1)
    {
        std::cout << "Usage: SUBSCRIBE <topic>\n";
        return;
    }

    std::string command = "SUBSCRIBE " + args[0];
    send_command(command);
}

/**
 * @brief Unsubscribe command Handler
 *
 * @param args Topic name
 */
void handle_unsubscribe(std::vector<std::string> args)
{
    if (args.size() != 1)
    {
        std::cout << "Invalid UNSUBSCRIBE command. Use:\n  UNSUBSCRIBE <topic>\n";
        return;
    }

    std::string command = "UNSUBSCRIBE " + args[0];
    send_command(command);
}

/**
 * @brief Sends a command to the server
 *
 * @param command Any command defined by the handlers
 */
void send_command(const std::string &command)
{
    std::lock_guard<std::mutex> lock(socket_mutex);
    if (!connected || global_socket == nullptr)
    {
        std::cout << "ERROR: Not connected to any server.\n";
        return;
    }
    if (command.empty())
        return;

    try
    {
        std::string formatted_command = command + "\n";
        boost::asio::write(*global_socket, boost::asio::buffer(formatted_command));
    }
    catch (std::exception &)
    {
        std::cerr << "[ERROR] Failed to send command. Connection lost.\n";
        cleanup_connection();
    }
}

/**
 * @brief Utility function to clean up the socket safely
 *
 */
void cleanup_connection()
{
    std::lock_guard<std::mutex> lock(socket_mutex);
    if (global_socket)
    {
        try
        {
            global_socket->close();
        }
        catch (...)
        {
        }
        delete global_socket;
        global_socket = nullptr;
    }
    connected = false;
}
