#include <iostream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <functional>
#include <boost/asio.hpp>
#include "argparse/argparse.hpp"

#define MAX_TOPIC_LENGTH 64
#define MAX_MESSAGE_LENGTH 1024

using boost::asio::ip::tcp;
using CommandHandler = std::function<void(std::shared_ptr<tcp::socket>, const std::string &)>;
struct ClientInfo
{
    std::shared_ptr<tcp::socket> socket;
    std::string name;
    int pid;
};

struct ClientMetadata
{
    std::string name;
    std::string ip;
    int client_pid;
    int client_port;
    int server_port;
};

// Maps for storing client info and topic subscriptions
std::unordered_map<std::shared_ptr<tcp::socket>, ClientInfo> connected_clients;
std::unordered_map<std::string, std::vector<std::shared_ptr<tcp::socket>>> topic_subscribers;

// Command handler map
std::unordered_map<std::string, CommandHandler> command_handlers;

// Mutexes
std::mutex topic_mutex, client_mutex;

// Function declarations
void start_server(boost::asio::io_context &io_context, int port);
void client_handler(std::shared_ptr<tcp::socket> socket);

void setup_command_handlers();
void handle_connect(std::shared_ptr<tcp::socket> socket, const std::string &args);
void handle_disconnect(std::shared_ptr<tcp::socket> socket, const std::string &);
void handle_subscribe(std::shared_ptr<tcp::socket> socket, std::string topic);
void handle_unsubscribe(std::shared_ptr<tcp::socket> socket, std::string topic);
void handle_publish(std::shared_ptr<tcp::socket> socket, const std::string &args);

void send_message(std::shared_ptr<tcp::socket> socket, const std::string &message);

std::string sanitize_topic(const std::string &topic);
std::string sanitize_message(const std::string &message);
ClientMetadata get_client_metadata(std::shared_ptr<tcp::socket> socket);
void log_action(const std::string &action, const ClientMetadata &client, const std::string &details);

/**
 * @brief Topic Server Application
 *
 * @param argc Argument Count
 * @param argv Argument List
 * @return int Status
 */
int main(int argc, char *argv[])
{
    // Argument parsing using argparse
    argparse::ArgumentParser program("server", "1.0.1-nightly");

    program.add_argument("-l", "--listen")
        .default_value(1999)
        .scan<'i', int>()
        .help("Port number to listen on");

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

    int port = program.get<int>("--listen");

    try
    {
        setup_command_handlers();
        boost::asio::io_context io_context;
        start_server(io_context, port);
    }
    catch (std::exception &e)
    {
        std::cerr << "Server error: " << e.what() << std::endl;
    }

    return 0;
}

/**
 * @brief Start the server on a port
 *
 * @param io_context The io_context class provides the core I/O functionality
 * for users of the asynchronous I/O objects
 * @param port Server listening port
 */
void start_server(boost::asio::io_context &io_context, int port)
{
    tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), port));

    std::cout << "Server started on port " << port << std::endl;

    while (true)
    {
        auto socket = std::make_shared<tcp::socket>(io_context);
        acceptor.accept(*socket);
        std::thread(client_handler, socket).detach();
    }
}

/**
 * @brief Handles interactions with the client
 *
 * @param socket TCP Socket
 */
void client_handler(std::shared_ptr<tcp::socket> socket)
{
    try
    {
        char data[1024];

        while (true)
        {
            boost::system::error_code error;
            size_t length = socket->read_some(boost::asio::buffer(data), error);

            if (error == boost::asio::error::eof)
            {
                ClientMetadata clinet = get_client_metadata(socket);
                log_action("DISCONNECT", clinet, error.message());
                break;
            }
            else if (error)
            {
                throw boost::system::system_error(error);
            }

            std::string message(data, length);
            message.erase(std::remove(message.begin(), message.end(), '\n'), message.end()); // Trim newlines

            std::cout << "[received] '" << message << "'" << std::endl;

            size_t space1 = message.find(' ');
            std::string command = (space1 == std::string::npos) ? message : message.substr(0, space1);
            std::string args = (space1 == std::string::npos) ? "" : message.substr(space1 + 1);

            auto it = command_handlers.find(command);
            if (it != command_handlers.end())
            {
                it->second(socket, args);
            }
            else
            {
                send_message(socket, "[SERVER_ERROR] Unknown command: " + command);
            }
        }
    }
    catch (std::exception &e)
    {
        std::cerr << "Client error: " << e.what() << std::endl;
    }
}

/**
 * @brief Initialize command handlers
 *
 */
void setup_command_handlers()
{
    command_handlers["CONNECT"] = handle_connect;
    command_handlers["DISCONNECT"] = handle_disconnect;
    command_handlers["SUBSCRIBE"] = handle_subscribe;
    command_handlers["UNSUBSCRIBE"] = handle_unsubscribe;
    command_handlers["PUBLISH"] = handle_publish;
}

/**
 * @brief Connect command Handler
 *
 * @param socket TCP Socket
 * @param args Client connection parameters
 */
void handle_connect(std::shared_ptr<tcp::socket> socket, const std::string &args)
{
    std::lock_guard<std::mutex> lock(client_mutex);

    std::istringstream iss(args);
    int client_port;
    std::string client_name;
    int client_pid;

    // Ensure correct parsing of: CONNECT <serverPort> <clientName> <PID>
    if (!(iss >> client_port >> client_name >> client_pid))
    {
        ClientMetadata client = get_client_metadata(socket);
        log_action("CONNECTION_ERROR", client, "Client connect message is malformed.");
        return;
    }

    // Ensure unique client name (append `-PID` if duplicate)
    std::string original_name = client_name;
    for (const auto &pair : connected_clients)
    {
        if (pair.second.name == client_name)
        {
            client_name = original_name + "-" + std::to_string(client_pid);
            break;
        }
    }

    // Store client info
    connected_clients[socket] = {socket, client_name, client_pid};

    ClientMetadata client = get_client_metadata(socket);
    log_action("CONNECT", client, "success");

    send_message(socket, "[SERVER] Connected as " + client_name);
}

/**
 * @brief Disconnect command Handler
 * Disconnects a client form the server
 *
 * @param socket TCP Socket
 */
void handle_disconnect(std::shared_ptr<tcp::socket> socket, const std::string &)
{
    std::lock_guard<std::mutex> lock(client_mutex);
    auto it = connected_clients.find(socket);

    if (it != connected_clients.end())
    {
        ClientMetadata client = get_client_metadata(socket);

        // Remove client from all topics
        {
            std::lock_guard<std::mutex> topic_lock(topic_mutex);
            for (auto &pair : topic_subscribers)
            {
                pair.second.erase(std::remove(pair.second.begin(), pair.second.end(), socket), pair.second.end());
            }
        }

        log_action("DISCONNECT", client, "success");

        connected_clients.erase(it);
        send_message(socket, "[SERVER] Disconnected");
    }
}

/**
 * @brief Subscribe command Handler
 * Subscribes a client to a topic and and if topic is non existant creates a new one
 *
 * @param socket TCP Socket
 * @param topic Topic name
 */
void handle_subscribe(std::shared_ptr<tcp::socket> socket, std::string topic)
{
    topic = sanitize_topic(topic);
    if (topic.empty())
    {
        send_message(socket, "[SERVER_ERROR] Invalid topic. Only letters (A-Z, a-z), numbers (0-9), and max length of 64 are allowed.");
        return;
    }

    std::lock_guard<std::mutex> lock(topic_mutex);

    auto &subscribers = topic_subscribers[topic];

    // Check if the socket is already subscribed using `get()` to compare raw pointers
    auto it = std::find_if(subscribers.begin(), subscribers.end(),
                           [&](const std::shared_ptr<tcp::socket> &s)
                           { return s.get() == socket.get(); });

    if (it == subscribers.end()) // Only add if not already subscribed
    {
        subscribers.push_back(socket);
    }
    else
    {
        send_message(socket, "[SERVER] Already subscribed to " + topic);
        return;
    }

    // Fetch client metadata
    ClientMetadata client = get_client_metadata(socket);
    log_action("SUBSCRIBE", client, "Topic: " + topic);

    send_message(socket, "[SERVER] Subscribed to " + topic);
}

/**
 * @brief Unsubscribe command Handler
 * Unsubscribes the client form the given topic
 *
 * @param socket TCP Socket
 * @param topic Topic name
 */
void handle_unsubscribe(std::shared_ptr<tcp::socket> socket, std::string topic)
{
    topic = sanitize_topic(topic);
    if (topic.empty())
    {
        send_message(socket, "[SERVER_ERROR] Invalid topic. Only letters (A-Z, a-z), numbers (0-9), and max length of 64 are allowed.");
        return;
    }

    std::lock_guard<std::mutex> lock(topic_mutex);

    auto it = topic_subscribers.find(topic);
    if (it == topic_subscribers.end() || it->second.empty())
    {
        send_message(socket, "[SERVER_ERROR] You are not subscribed to " + topic);
        return;
    }

    auto &subscribers = it->second;

    // Find and remove the correct socket using raw pointer comparison
    auto sub_it = std::remove_if(subscribers.begin(), subscribers.end(),
                                 [&](const std::shared_ptr<tcp::socket> &s)
                                 { return s.get() == socket.get(); });

    if (sub_it == subscribers.end())
    {
        send_message(socket, "[SERVER_ERROR] You are not subscribed to " + topic);
        return;
    }

    subscribers.erase(sub_it, subscribers.end());

    // Fetch client metadata
    ClientMetadata client = get_client_metadata(socket);
    log_action("UNSUBSCRIBE", client, topic);

    send_message(socket, "[SERVER] Unsubscribed from " + topic);
}

/**
 * @brief Publish command Handler
 * Publishes data to a topic and sends it to all subscribed clients
 *
 * @param socket TCP Socket
 * @param args Arguments are topic name and topic payload, all ASCII
 */
void handle_publish(std::shared_ptr<tcp::socket> socket, const std::string &args)
{
    size_t space = args.find(' ');
    if (space == std::string::npos)
    {
        send_message(socket, "[SERVER_ERROR] Invalid publish format! Topic or message missing.");
        return;
    }

    std::string topic = sanitize_topic(args.substr(0, space)); // Sanitize topic
    if (topic.empty())
    {
        send_message(socket, "[SERVER_ERROR] Invalid topic. Only letters (A-Z, a-z), numbers (0-9), and max length of 64 are allowed.");
        return;
    }

    std::string payload = sanitize_message(args.substr(space + 1)); // Sanitize message
    if (payload.empty())
    {
        send_message(socket, "[SERVER_ERROR] Invalid message. Only Base64 characters (A-Z, a-z, 0-9, +, /, =) and max length of 1024 are allowed.");
        return;
    }

    std::lock_guard<std::mutex> lock(topic_mutex);
    auto it = topic_subscribers.find(topic);

    if (it == topic_subscribers.end() || it->second.empty())
    {
        send_message(socket, "[SERVER_ERROR] No subscribers for topic: " + topic);
        return;
    }

    ClientMetadata client = get_client_metadata(socket);
    log_action("PUBLISH", client, "Topic: " + topic + " Message: " + payload);

    for (auto subscriber : it->second)
    {
        try
        {
            send_message(subscriber, "[Message] Topic: " + topic + " Data: " + payload);
        }
        catch (const std::exception &)
        {
            topic_subscribers[topic].erase(std::remove(topic_subscribers[topic].begin(), topic_subscribers[topic].end(), subscriber),
                                           topic_subscribers[topic].end());
        }
    }
}

/**
 * @brief Utility function to send messages to a client
 *
 * @param socket TCP Socket
 * @param message Message that is sent to the client
 */
void send_message(std::shared_ptr<tcp::socket> socket, const std::string &message)
{
    boost::asio::write(*socket, boost::asio::buffer(message + "\n"));
}

/**
 * @brief Sanitize topic name and restrict it
 *
 * @param topic Topic name
 * @return std::string
 */
std::string sanitize_topic(const std::string &topic)
{
    // Trim leading and trailing spaces
    size_t first = topic.find_first_not_of(" \t");
    size_t last = topic.find_last_not_of(" \t");

    if (first == std::string::npos || last == std::string::npos)
    {
        return ""; // Topic is empty or all spaces
    }

    std::string clean_topic = topic.substr(first, last - first + 1);

    // Enforce max length restriction
    if (clean_topic.length() > MAX_TOPIC_LENGTH)
        return ""; // Reject overly long topics

    // Ensure only alphanumeric characters are used
    if (!std::all_of(clean_topic.begin(), clean_topic.end(), ::isalnum))
    {
        return ""; // Invalid topic, contains non-alphanumeric characters
    }

    return clean_topic;
}

/**
 * @brief Utility function to sanitize
 *
 * @param message Message to be sanitized
 * @return std::string sanitized message
 */
std::string sanitize_message(const std::string &message)
{
    // Trim leading and trailing spaces
    size_t first = message.find_first_not_of(" \t");
    size_t last = message.find_last_not_of(" \t");

    if (first == std::string::npos || last == std::string::npos)
        return ""; // Message is empty or all spaces

    std::string clean_message = message.substr(first, last - first + 1);

    // Enforce max length restriction
    if (clean_message.length() > MAX_MESSAGE_LENGTH)
    {
        return ""; // Reject overly long messages
    }

    // Ensure only valid ASCII characters (0x20 to 0x7E)
    for (char c : clean_message)
    {
        if (c < 0x20 || c > 0x7E)
        {
            return ""; // Reject invalid ASCII characters
        }
    }

    return clean_message;
}

/**
 * @brief Gets metadata form thee client
 *
 * @param socket TCP Socket
 * @return ClientMetadata Clients metadata structure
 */
ClientMetadata get_client_metadata(std::shared_ptr<tcp::socket> socket)
{
    ClientMetadata metadata;
    auto it = connected_clients.find(socket);
    if (it != connected_clients.end())
    {
        metadata.name = it->second.name;
        metadata.ip = socket->remote_endpoint().address().to_string();
        metadata.client_pid = it->second.pid;
        metadata.client_port = socket->remote_endpoint().port();
        metadata.server_port = socket->local_endpoint().port();
    }
    return metadata;
}

/**
 * @brief Utility function to log client actions
 *
 * @param action Action category
 * @param client clinet doing the action
 * @param details What is going on
 */
void log_action(const std::string &action, const ClientMetadata &client, const std::string &details = "")
{
    std::cout << "[" << action << "] "
              << "(" << details << ") "
              << "Client: " << client.name << ", "
              << "PID: " << client.client_pid << ", "
              << "IP: " << client.ip << ", "
              << "PORT: " << client.client_port << ", "
              << "SERVER_PORT: " << client.server_port
              << std::endl;
}
