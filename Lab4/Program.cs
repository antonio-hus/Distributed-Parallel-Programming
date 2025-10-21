///////////////////////////
///   IMPORTS SECTION   ///
///////////////////////////
using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

///////////////////////////
///   STRUCTS SECTION   ///
///////////////////////////

/// <summary>
/// Represents a single HTTP download request configuration.
/// Contains all necessary information to establish connection and retrieve content.
/// </summary>
struct DownloadRequest
{
    /// <summary>Hostname or IP address of the server</summary>
    public string Host;
    
    /// <summary>Path to the resource on the server (e.g., "/index.html")</summary>
    public string Path;
    
    /// <summary>Port number for HTTP connection (typically 80)</summary>
    public int Port;
    
    /// <summary>Unique identifier for logging and tracking purposes</summary>
    public string Id;
}

/// <summary>
/// Stores the complete result of an HTTP download operation.
/// Contains both raw response data and parsed HTTP headers.
/// </summary>
struct DownloadResult
{
    /// <summary>Request identifier for correlation with original request</summary>
    public string Id;
    
    /// <summary>Complete HTTP response body content</summary>
    public string Content;
    
    /// <summary>HTTP status code (e.g., 200 for OK, 404 for Not Found)</summary>
    public int StatusCode;
    
    /// <summary>Size of the response body in bytes (from Content-Length header)</summary>
    public int ContentLength;
    
    /// <summary>Flag indicating whether the download completed successfully</summary>
    public bool Success;
    
    /// <summary>Error message if download failed, null otherwise</summary>
    public string Error;
}

/// <summary>
/// Internal state machine for HTTP protocol parsing.
/// Tracks progress through connection, request sending, header parsing, and body reception.
/// </summary>
enum ParserState
{
    /// <summary>Initial state: socket not yet connected</summary>
    NotConnected,
    
    /// <summary>Connection established, ready to send HTTP request</summary>
    Connected,
    
    /// <summary>HTTP request sent, awaiting response headers</summary>
    RequestSent,
    
    /// <summary>Currently reading and parsing HTTP headers</summary>
    ReadingHeaders,
    
    /// <summary>Headers parsed, reading response body content</summary>
    ReadingBody,
    
    /// <summary>Download complete, connection can be closed</summary>
    Done
}

///////////////////////////
///  IMPLEMENTATION 1   ///
///  DIRECT CALLBACKS   ///
///////////////////////////

/// <summary>
/// STRATEGY 1: Event-Driven Direct Callback Implementation
/// 
/// This implementation uses raw asynchronous socket operations with direct callbacks.
/// Each asynchronous operation (Connect, Send, Receive) triggers a callback that
/// advances the state machine and initiates the next operation.
/// 
/// ADVANTAGES:
/// - Direct control over execution flow
/// - Minimal overhead (no task allocation)
/// - Explicit state management
/// 
/// DISADVANTAGES:
/// - Complex "callback hell" structure
/// - Difficult to follow control flow
/// - Manual state machine management
/// - Hard to compose operations
/// 
/// SYNCHRONIZATION:
/// - No locks needed (each download operates on isolated state)
/// - Callbacks execute sequentially per socket (no concurrent access to same state)
/// </summary>
class HttpDownloaderCallbacks
{
    private Socket _socket;
    private DownloadRequest _request;
    private byte[] _buffer;
    private StringBuilder _receivedData;
    private ParserState _state;
    private Action<DownloadResult> _onComplete;
    
    // HTTP parsing state
    private int _contentLength;
    private int _statusCode;
    private int _headerEndIndex;
    private int _bodyBytesReceived;

    /// <summary>
    /// Initializes a new HTTP downloader with callback-based implementation.
    /// </summary>
    /// <param name="request">Download request configuration</param>
    /// <param name="onComplete">Callback invoked when download completes (success or failure)</param>
    private HttpDownloaderCallbacks(DownloadRequest request, Action<DownloadResult> onComplete)
    {
        _request = request;
        _onComplete = onComplete;
        _buffer = new byte[8192]; // 8KB receive buffer
        _receivedData = new StringBuilder();
        _state = ParserState.NotConnected;
        _contentLength = -1;
        _statusCode = 0;
        _headerEndIndex = -1;
        _bodyBytesReceived = 0;
    }

    /// <summary>
    /// Initiates an asynchronous HTTP download using direct callbacks.
    /// This is the public entry point for the callback-based implementation.
    /// </summary>
    public static void Download(DownloadRequest request, Action<DownloadResult> onComplete)
    {
        HttpDownloaderCallbacks downloader = new HttpDownloaderCallbacks(request, onComplete);
        downloader.Start();
    }

    /// <summary>
    /// Begins the download process by initiating the TCP connection.
    /// </summary>
    private void Start()
    {
        try
        {
            _socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
            
            // Resolve hostname to IP address
            IPAddress[] addresses = Dns.GetHostAddresses(_request.Host);
            IPEndPoint endpoint = new IPEndPoint(addresses[0], _request.Port);
            
            Console.WriteLine($"[{_request.Id}] Connecting to {_request.Host}:{_request.Port}...");
            
            // Initiate asynchronous connection
            _socket.BeginConnect(endpoint, OnConnected, null);
        }
        catch (Exception ex)
        {
            CompleteWithError($"Connection initialization failed: {ex.Message}");
        }
    }

    /// <summary>
    /// Callback invoked when TCP connection completes (or fails).
    /// Advances state machine to send HTTP request.
    /// </summary>
    private void OnConnected(IAsyncResult ar)
    {
        try
        {
            _socket.EndConnect(ar);
            _state = ParserState.Connected;
            
            Console.WriteLine($"[{_request.Id}] Connected successfully");
            
            // Build HTTP GET request following RFC 2616 format
            string request = $"GET {_request.Path} HTTP/1.1\r\n" +
                           $"Host: {_request.Host}\r\n" +
                           $"Connection: close\r\n" +
                           $"\r\n";
            
            byte[] requestBytes = Encoding.ASCII.GetBytes(request);
            
            Console.WriteLine($"[{_request.Id}] Sending HTTP request...");
            
            // Initiate asynchronous send operation
            _socket.BeginSend(requestBytes, 0, requestBytes.Length, SocketFlags.None, OnRequestSent, null);
        }
        catch (Exception ex)
        {
            CompleteWithError($"Connection failed: {ex.Message}");
        }
    }

    /// <summary>
    /// Callback invoked when HTTP request has been sent.
    /// Advances state machine to begin receiving response.
    /// </summary>
    private void OnRequestSent(IAsyncResult ar)
    {
        try
        {
            int bytesSent = _socket.EndSend(ar);
            _state = ParserState.RequestSent;
            
            Console.WriteLine($"[{_request.Id}] Request sent ({bytesSent} bytes), waiting for response...");
            
            // Begin receiving HTTP response
            _socket.BeginReceive(_buffer, 0, _buffer.Length, SocketFlags.None, OnDataReceived, null);
        }
        catch (Exception ex)
        {
            CompleteWithError($"Send failed: {ex.Message}");
        }
    }

    /// <summary>
    /// Callback invoked when data is received from the server.
    /// This is the core state machine that handles:
    /// 1. HTTP header parsing (status line + headers)
    /// 2. Content-Length extraction
    /// 3. Body data accumulation
    /// 4. Completion detection
    /// 
    /// This callback may be invoked multiple times as data arrives in chunks.
    /// </summary>
    private void OnDataReceived(IAsyncResult ar)
    {
        try
        {
            int bytesRead = _socket.EndReceive(ar);
            
            // Zero bytes indicates server closed connection
            if (bytesRead == 0)
            {
                Console.WriteLine($"[{_request.Id}] Connection closed by server");
                CompleteDownload();
                return;
            }
            
            // Append received data to accumulation buffer
            string chunk = Encoding.ASCII.GetString(_buffer, 0, bytesRead);
            _receivedData.Append(chunk);
            
            // STATE MACHINE: Process data based on current parsing state
            if (_state == ParserState.RequestSent || _state == ParserState.ReadingHeaders)
            {
                // Still parsing headers - look for end of headers marker "\r\n\r\n"
                string currentData = _receivedData.ToString();
                _headerEndIndex = currentData.IndexOf("\r\n\r\n");
                
                if (_headerEndIndex != -1)
                {
                    // Headers complete - parse them
                    string headers = currentData.Substring(0, _headerEndIndex);
                    ParseHeaders(headers);
                    
                    _state = ParserState.ReadingBody;
                    
                    // Calculate how many body bytes we already have
                    int bodyStartIndex = _headerEndIndex + 4; // Skip "\r\n\r\n"
                    _bodyBytesReceived = currentData.Length - bodyStartIndex;
                    
                    Console.WriteLine($"[{_request.Id}] Headers parsed. Status: {_statusCode}, Content-Length: {_contentLength}, Body received: {_bodyBytesReceived}/{_contentLength}");
                }
                else
                {
                    _state = ParserState.ReadingHeaders;
                }
            }
            else if (_state == ParserState.ReadingBody)
            {
                // Accumulating body data
                _bodyBytesReceived += bytesRead;
                
                Console.WriteLine($"[{_request.Id}] Receiving body: {_bodyBytesReceived}/{_contentLength} bytes");
            }
            
            // Check if download is complete
            if (_state == ParserState.ReadingBody && _contentLength > 0 && _bodyBytesReceived >= _contentLength)
            {
                Console.WriteLine($"[{_request.Id}] Download complete!");
                CompleteDownload();
                return;
            }
            
            // Continue receiving more data
            _socket.BeginReceive(_buffer, 0, _buffer.Length, SocketFlags.None, OnDataReceived, null);
        }
        catch (Exception ex)
        {
            CompleteWithError($"Receive failed: {ex.Message}");
        }
    }

    /// <summary>
    /// Parses HTTP response headers to extract status code and Content-Length.
    /// Implements minimal HTTP/1.1 header parsing as per RFC 2616.
    /// </summary>
    /// <param name="headers">Raw HTTP header text (status line + header lines)</param>
    private void ParseHeaders(string headers)
    {
        string[] lines = headers.Split(new[] { "\r\n" }, StringSplitOptions.RemoveEmptyEntries);
        
        if (lines.Length > 0)
        {
            // Parse status line: "HTTP/1.1 200 OK"
            string[] statusParts = lines[0].Split(' ');
            if (statusParts.Length >= 2)
            {
                int.TryParse(statusParts[1], out _statusCode);
            }
        }
        
        // Parse header lines looking for Content-Length
        foreach (string line in lines.Skip(1))
        {
            if (line.StartsWith("Content-Length:", StringComparison.OrdinalIgnoreCase))
            {
                string value = line.Substring(15).Trim();
                int.TryParse(value, out _contentLength);
            }
        }
    }

    /// <summary>
    /// Completes the download successfully by extracting the body content
    /// and invoking the completion callback.
    /// </summary>
    private void CompleteDownload()
    {
        _state = ParserState.Done;
        _socket.Close();
        
        // Extract body content (everything after headers)
        string fullResponse = _receivedData.ToString();
        string body = "";
        
        if (_headerEndIndex != -1)
        {
            int bodyStart = _headerEndIndex + 4;
            body = fullResponse.Substring(bodyStart);
        }
        
        DownloadResult result = new DownloadResult
        {
            Id = _request.Id,
            Content = body,
            StatusCode = _statusCode,
            ContentLength = _contentLength,
            Success = true,
            Error = null
        };
        
        _onComplete(result);
    }

    /// <summary>
    /// Completes the download with an error, invoking the completion callback
    /// with failure status and error message.
    /// </summary>
    private void CompleteWithError(string error)
    {
        _state = ParserState.Done;
        
        if (_socket != null)
        {
            _socket.Close();
        }
        
        DownloadResult result = new DownloadResult
        {
            Id = _request.Id,
            Content = null,
            StatusCode = _statusCode,
            ContentLength = 0,
            Success = false,
            Error = error
        };
        
        _onComplete(result);
    }
}

///////////////////////////
///  IMPLEMENTATION 2   ///
///  TASK-BASED (BONUS) ///
///////////////////////////

/// <summary>
/// STRATEGY 2: Task-Based with ContinueWith() Chaining
/// 
/// This implementation wraps asynchronous socket operations in Task objects,
/// then chains them together using ContinueWith() to create a pipeline.
/// Each operation returns a Task that completes when the async operation finishes.
/// 
/// ADVANTAGES:
/// - Composable task chains
/// - Better control flow compared to raw callbacks
/// - Can use Task combinators (WhenAll, WhenAny, etc.)
/// - Exception propagation through task chain
/// 
/// DISADVANTAGES:
/// - More complex than async/await
/// - Still requires explicit continuation management
/// - Need to unwrap nested tasks
/// 
/// SYNCHRONIZATION:
/// - Tasks provide inherent serialization of operations
/// - Each continuation runs after its predecessor completes
/// - No explicit locks needed (isolated state per download)
/// </summary>
class HttpDownloaderTasks
{
    /// <summary>
    /// Initiates an asynchronous HTTP download using task-based implementation.
    /// Returns a Task that completes when the download finishes (success or failure).
    /// </summary>
    public static Task<DownloadResult> Download(DownloadRequest request)
    {
        Console.WriteLine($"[{request.Id}] Starting task-based download...");
        
        // TASK CHAIN ARCHITECTURE:
        // 1. ConnectAsync() -> Task<Socket>
        // 2. ContinueWith: SendRequestAsync() -> Task<Socket>
        // 3. ContinueWith: ReceiveResponseAsync() -> Task<string>
        // 4. ContinueWith: ParseResponse() -> Task<DownloadResult>
        //
        // Each stage depends on the previous stage's result.
        // Exceptions in any stage propagate to the final result.
        
        return ConnectAsync(request.Host, request.Port, request.Id)
            .ContinueWith((Task<Socket> connectTask) =>
            {
                if (connectTask.IsFaulted)
                {
                    return Task.FromResult(new DownloadResult
                    {
                        Id = request.Id,
                        Success = false,
                        Error = $"Connection failed: {connectTask.Exception?.GetBaseException().Message}"
                    });
                }
                
                Socket socket = connectTask.Result;
                return SendRequestAsync(socket, request, request.Id)
                    .ContinueWith((Task<Socket> sendTask) =>
                    {
                        if (sendTask.IsFaulted)
                        {
                            socket.Close();
                            return Task.FromResult(new DownloadResult
                            {
                                Id = request.Id,
                                Success = false,
                                Error = $"Send failed: {sendTask.Exception?.GetBaseException().Message}"
                            });
                        }
                        
                        return ReceiveResponseAsync(socket, request.Id)
                            .ContinueWith((Task<string> receiveTask) =>
                            {
                                socket.Close();
                                
                                if (receiveTask.IsFaulted)
                                {
                                    return new DownloadResult
                                    {
                                        Id = request.Id,
                                        Success = false,
                                        Error = $"Receive failed: {receiveTask.Exception?.GetBaseException().Message}"
                                    };
                                }
                                
                                return ParseResponse(receiveTask.Result, request.Id);
                            });
                    }).Unwrap(); // Unwrap nested Task<Task<DownloadResult>> to Task<DownloadResult>
            }).Unwrap();
    }

    /// <summary>
    /// Wraps Socket.BeginConnect/EndConnect in a Task.
    /// Returns a Task that completes with the connected socket.
    /// </summary>
    private static Task<Socket> ConnectAsync(string host, int port, string id)
    {
        TaskCompletionSource<Socket> tcs = new TaskCompletionSource<Socket>();
        
        try
        {
            Socket socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
            IPAddress[] addresses = Dns.GetHostAddresses(host);
            IPEndPoint endpoint = new IPEndPoint(addresses[0], port);
            
            Console.WriteLine($"[{id}] Connecting to {host}:{port}...");
            
            socket.BeginConnect(endpoint, (IAsyncResult ar) =>
            {
                try
                {
                    socket.EndConnect(ar);
                    Console.WriteLine($"[{id}] Connected successfully");
                    tcs.SetResult(socket); // Complete the task with the connected socket
                }
                catch (Exception ex)
                {
                    tcs.SetException(ex); // Propagate exception through task
                }
            }, null);
        }
        catch (Exception ex)
        {
            tcs.SetException(ex);
        }
        
        return tcs.Task;
    }

    /// <summary>
    /// Wraps Socket.BeginSend/EndSend in a Task.
    /// Sends HTTP GET request and returns a Task that completes with the socket.
    /// </summary>
    private static Task<Socket> SendRequestAsync(Socket socket, DownloadRequest request, string id)
    {
        TaskCompletionSource<Socket> tcs = new TaskCompletionSource<Socket>();
        
        try
        {
            string httpRequest = $"GET {request.Path} HTTP/1.1\r\n" +
                               $"Host: {request.Host}\r\n" +
                               $"Connection: close\r\n" +
                               $"\r\n";
            
            byte[] requestBytes = Encoding.ASCII.GetBytes(httpRequest);
            
            Console.WriteLine($"[{id}] Sending HTTP request...");
            
            socket.BeginSend(requestBytes, 0, requestBytes.Length, SocketFlags.None, (IAsyncResult ar) =>
            {
                try
                {
                    int bytesSent = socket.EndSend(ar);
                    Console.WriteLine($"[{id}] Request sent ({bytesSent} bytes)");
                    tcs.SetResult(socket); // Pass socket to next stage
                }
                catch (Exception ex)
                {
                    tcs.SetException(ex);
                }
            }, null);
        }
        catch (Exception ex)
        {
            tcs.SetException(ex);
        }
        
        return tcs.Task;
    }

    /// <summary>
    /// Wraps Socket.BeginReceive/EndReceive in a Task.
    /// Receives complete HTTP response (headers + body) and returns it as a string.
    /// 
    /// This method implements a recursive receive loop using task continuations.
    /// Each receive operation schedules the next receive until all data is received.
    /// </summary>
    private static Task<string> ReceiveResponseAsync(Socket socket, string id)
    {
        TaskCompletionSource<string> tcs = new TaskCompletionSource<string>();
        byte[] buffer = new byte[8192];
        StringBuilder receivedData = new StringBuilder();
        
        // Helper function to recursively receive data
        Action<int, int> receiveLoop = null;
        receiveLoop = (contentLength, bodyBytesReceived) =>
        {
            socket.BeginReceive(buffer, 0, buffer.Length, SocketFlags.None, (IAsyncResult ar) =>
            {
                try
                {
                    int bytesRead = socket.EndReceive(ar);
                    
                    if (bytesRead == 0)
                    {
                        // Connection closed - complete the task
                        Console.WriteLine($"[{id}] Connection closed by server");
                        tcs.SetResult(receivedData.ToString());
                        return;
                    }
                    
                    string chunk = Encoding.ASCII.GetString(buffer, 0, bytesRead);
                    receivedData.Append(chunk);
                    
                    // Check if we have complete headers
                    string currentData = receivedData.ToString();
                    int headerEndIndex = currentData.IndexOf("\r\n\r\n");
                    
                    if (headerEndIndex != -1 && contentLength == -1)
                    {
                        // Parse Content-Length from headers
                        string headers = currentData.Substring(0, headerEndIndex);
                        contentLength = ParseContentLength(headers);
                        bodyBytesReceived = currentData.Length - (headerEndIndex + 4);
                        
                        Console.WriteLine($"[{id}] Headers received, Content-Length: {contentLength}, Body: {bodyBytesReceived}/{contentLength} bytes");
                    }
                    else if (contentLength > 0)
                    {
                        bodyBytesReceived += bytesRead;
                        Console.WriteLine($"[{id}] Receiving body: {bodyBytesReceived}/{contentLength} bytes");
                    }
                    
                    // Check if download is complete
                    if (contentLength > 0 && bodyBytesReceived >= contentLength)
                    {
                        Console.WriteLine($"[{id}] Download complete!");
                        tcs.SetResult(receivedData.ToString());
                        return;
                    }
                    
                    // Continue receiving
                    receiveLoop(contentLength, bodyBytesReceived);
                }
                catch (Exception ex)
                {
                    tcs.SetException(ex);
                }
            }, null);
        };
        
        // Start the receive loop
        receiveLoop(-1, 0);
        
        return tcs.Task;
    }

    /// <summary>
    /// Parses Content-Length header from HTTP response headers.
    /// </summary>
    private static int ParseContentLength(string headers)
    {
        string[] lines = headers.Split(new[] { "\r\n" }, StringSplitOptions.RemoveEmptyEntries);
        
        foreach (string line in lines)
        {
            if (line.StartsWith("Content-Length:", StringComparison.OrdinalIgnoreCase))
            {
                string value = line.Substring(15).Trim();
                if (int.TryParse(value, out int length))
                {
                    return length;
                }
            }
        }
        
        return -1;
    }

    /// <summary>
    /// Parses complete HTTP response into a DownloadResult.
    /// Extracts status code, Content-Length, and body content.
    /// </summary>
    private static DownloadResult ParseResponse(string response, string id)
    {
        int headerEndIndex = response.IndexOf("\r\n\r\n");
        
        if (headerEndIndex == -1)
        {
            return new DownloadResult
            {
                Id = id,
                Success = false,
                Error = "Invalid HTTP response (no headers found)"
            };
        }
        
        string headers = response.Substring(0, headerEndIndex);
        string body = response.Substring(headerEndIndex + 4);
        
        // Parse status code
        string[] lines = headers.Split(new[] { "\r\n" }, StringSplitOptions.RemoveEmptyEntries);
        int statusCode = 0;
        
        if (lines.Length > 0)
        {
            string[] statusParts = lines[0].Split(' ');
            if (statusParts.Length >= 2)
            {
                int.TryParse(statusParts[1], out statusCode);
            }
        }
        
        // Parse Content-Length
        int contentLength = ParseContentLength(headers);
        
        return new DownloadResult
        {
            Id = id,
            Content = body,
            StatusCode = statusCode,
            ContentLength = contentLength,
            Success = true,
            Error = null
        };
    }
}

///////////////////////////
///  IMPLEMENTATION 3   ///
///   ASYNC/AWAIT       ///
///////////////////////////

/// <summary>
/// STRATEGY 3: Async/Await Implementation
/// 
/// This implementation uses C#'s async/await keywords to write asynchronous code
/// that looks like synchronous code. The compiler transforms async methods into
/// state machines similar to Implementation 1, but handles all the complexity automatically.
/// 
/// ADVANTAGES:
/// - Most readable and maintainable
/// - Sequential control flow (no callback hell)
/// - Automatic exception handling
/// - Compiler-generated state machine
/// - Easy to compose operations
/// 
/// DISADVANTAGES:
/// - Slight overhead from compiler-generated machinery
/// - Requires understanding of async/await semantics
/// 
/// SYNCHRONIZATION:
/// - Async methods provide inherent serialization
/// - Each await suspends execution until operation completes
/// - No explicit locks needed (isolated state per download)
/// 
/// This is the RECOMMENDED approach for production code.
/// </summary>
class HttpDownloaderAsync
{
    /// <summary>
    /// Initiates an asynchronous HTTP download using async/await.
    /// Returns a Task that completes when the download finishes.
    /// 
    /// Note: This method looks like synchronous code but is fully asynchronous.
    /// Each 'await' suspends execution and returns control to the caller,
    /// resuming when the awaited operation completes.
    /// </summary>
    public static async Task<DownloadResult> Download(DownloadRequest request)
    {
        Socket socket = null;
        
        try
        {
            Console.WriteLine($"[{request.Id}] Starting async download...");
            
            // Step 1: Connect to server
            socket = await ConnectAsync(request.Host, request.Port, request.Id);
            
            // Step 2: Send HTTP request
            await SendRequestAsync(socket, request, request.Id);
            
            // Step 3: Receive complete response
            string response = await ReceiveResponseAsync(socket, request.Id);
            
            // Step 4: Parse response
            DownloadResult result = ParseResponse(response, request.Id);
            
            return result;
        }
        catch (Exception ex)
        {
            return new DownloadResult
            {
                Id = request.Id,
                Success = false,
                Error = $"Download failed: {ex.Message}"
            };
        }
        finally
        {
            // Ensure socket is closed even if exception occurs
            if (socket != null)
            {
                socket.Close();
            }
        }
    }

    /// <summary>
    /// Asynchronously connects to the specified host and port.
    /// Wraps BeginConnect/EndConnect in an awaitable Task.
    /// </summary>
    private static Task<Socket> ConnectAsync(string host, int port, string id)
    {
        TaskCompletionSource<Socket> tcs = new TaskCompletionSource<Socket>();
        
        try
        {
            Socket socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
            IPAddress[] addresses = Dns.GetHostAddresses(host);
            IPEndPoint endpoint = new IPEndPoint(addresses[0], port);
            
            Console.WriteLine($"[{id}] Connecting to {host}:{port}...");
            
            socket.BeginConnect(endpoint, (IAsyncResult ar) =>
            {
                try
                {
                    socket.EndConnect(ar);
                    Console.WriteLine($"[{id}] Connected successfully");
                    tcs.SetResult(socket);
                }
                catch (Exception ex)
                {
                    tcs.SetException(ex);
                }
            }, null);
        }
        catch (Exception ex)
        {
            tcs.SetException(ex);
        }
        
        return tcs.Task;
    }

    /// <summary>
    /// Asynchronously sends an HTTP GET request.
    /// Wraps BeginSend/EndSend in an awaitable Task.
    /// </summary>
    private static Task SendRequestAsync(Socket socket, DownloadRequest request, string id)
    {
        TaskCompletionSource<int> tcs = new TaskCompletionSource<int>();
        
        try
        {
            string httpRequest = $"GET {request.Path} HTTP/1.1\r\n" +
                               $"Host: {request.Host}\r\n" +
                               $"Connection: close\r\n" +
                               $"\r\n";
            
            byte[] requestBytes = Encoding.ASCII.GetBytes(httpRequest);
            
            Console.WriteLine($"[{id}] Sending HTTP request...");
            
            socket.BeginSend(requestBytes, 0, requestBytes.Length, SocketFlags.None, (IAsyncResult ar) =>
            {
                try
                {
                    int bytesSent = socket.EndSend(ar);
                    Console.WriteLine($"[{id}] Request sent ({bytesSent} bytes)");
                    tcs.SetResult(bytesSent);
                }
                catch (Exception ex)
                {
                    tcs.SetException(ex);
                }
            }, null);
        }
        catch (Exception ex)
        {
            tcs.SetException(ex);
        }
        
        return tcs.Task;
    }

    /// <summary>
    /// Asynchronously receives the complete HTTP response.
    /// Implements a loop that continues receiving until all data is received.
    /// </summary>
    private static async Task<string> ReceiveResponseAsync(Socket socket, string id)
    {
        byte[] buffer = new byte[8192];
        StringBuilder receivedData = new StringBuilder();
        int contentLength = -1;
        int bodyBytesReceived = 0;
        
        while (true)
        {
            // Await next chunk of data
            int bytesRead = await ReceiveAsync(socket, buffer);
            
            if (bytesRead == 0)
            {
                // Connection closed
                Console.WriteLine($"[{id}] Connection closed by server");
                break;
            }
            
            string chunk = Encoding.ASCII.GetString(buffer, 0, bytesRead);
            receivedData.Append(chunk);
            
            // Check if we have complete headers
            string currentData = receivedData.ToString();
            int headerEndIndex = currentData.IndexOf("\r\n\r\n");
            
            if (headerEndIndex != -1 && contentLength == -1)
            {
                // Parse Content-Length from headers
                string headers = currentData.Substring(0, headerEndIndex);
                contentLength = ParseContentLength(headers);
                bodyBytesReceived = currentData.Length - (headerEndIndex + 4);
                
                Console.WriteLine($"[{id}] Headers received, Content-Length: {contentLength}, Body: {bodyBytesReceived}/{contentLength} bytes");
            }
            else if (contentLength > 0)
            {
                bodyBytesReceived += bytesRead;
                Console.WriteLine($"[{id}] Receiving body: {bodyBytesReceived}/{contentLength} bytes");
            }
            
            // Check if download is complete
            if (contentLength > 0 && bodyBytesReceived >= contentLength)
            {
                Console.WriteLine($"[{id}] Download complete!");
                break;
            }
        }
        
        return receivedData.ToString();
    }

    /// <summary>
    /// Wraps Socket.BeginReceive/EndReceive in an awaitable Task.
    /// </summary>
    private static Task<int> ReceiveAsync(Socket socket, byte[] buffer)
    {
        TaskCompletionSource<int> tcs = new TaskCompletionSource<int>();
        
        socket.BeginReceive(buffer, 0, buffer.Length, SocketFlags.None, (IAsyncResult ar) =>
        {
            try
            {
                int bytesRead = socket.EndReceive(ar);
                tcs.SetResult(bytesRead);
            }
            catch (Exception ex)
            {
                tcs.SetException(ex);
            }
        }, null);
        
        return tcs.Task;
    }

    /// <summary>
    /// Parses Content-Length header from HTTP response headers.
    /// </summary>
    private static int ParseContentLength(string headers)
    {
        string[] lines = headers.Split(new[] { "\r\n" }, StringSplitOptions.RemoveEmptyEntries);
        
        foreach (string line in lines)
        {
            if (line.StartsWith("Content-Length:", StringComparison.OrdinalIgnoreCase))
            {
                string value = line.Substring(15).Trim();
                if (int.TryParse(value, out int length))
                {
                    return length;
                }
            }
        }
        
        return -1;
    }

    /// <summary>
    /// Parses complete HTTP response into a DownloadResult.
    /// Extracts status code, Content-Length, and body content.
    /// </summary>
    private static DownloadResult ParseResponse(string response, string id)
    {
        int headerEndIndex = response.IndexOf("\r\n\r\n");
        
        if (headerEndIndex == -1)
        {
            return new DownloadResult
            {
                Id = id,
                Success = false,
                Error = "Invalid HTTP response (no headers found)"
            };
        }
        
        string headers = response.Substring(0, headerEndIndex);
        string body = response.Substring(headerEndIndex + 4);
        
        // Parse status code
        string[] lines = headers.Split(new[] { "\r\n" }, StringSplitOptions.RemoveEmptyEntries);
        int statusCode = 0;
        
        if (lines.Length > 0)
        {
            string[] statusParts = lines[0].Split(' ');
            if (statusParts.Length >= 2)
            {
                int.TryParse(statusParts[1], out statusCode);
            }
        }
        
        // Parse Content-Length
        int contentLength = ParseContentLength(headers);
        
        return new DownloadResult
        {
            Id = id,
            Content = body,
            StatusCode = statusCode,
            ContentLength = contentLength,
            Success = true,
            Error = null
        };
    }
}

/////////////////////////
///   MAIN SECTION    ///
/////////////////////////

/// <summary>
/// Main entry point: HTTP Downloader Stress Test
/// 
/// Demonstrates all three implementation strategies by downloading multiple files
/// concurrently from various web servers. Compares performance and verifies correctness.
/// </summary>
class Program
{
    static void Main(string[] args)
    {
        Console.WriteLine("========================================");
        Console.WriteLine("HTTP CONCURRENT DOWNLOADER");
        Console.WriteLine("========================================\n");
        
        // Define test URLs to download
        List<DownloadRequest> requests = new List<DownloadRequest>
        {
            new DownloadRequest { Host = "www.cs.ubbcluj.ro", Path = "/~rlupsa/edu/pdp/lab-4-futures-continuations.html", Port = 80, Id = "cs.ubbcluj.ro" },
            new DownloadRequest { Host = "www.textfiles.com", Path = "/100/914bbs.txt", Port = 80, Id = "textfiles.com" }
        };
        
        // Test all three implementations
        Console.WriteLine("\n==========================================");
        Console.WriteLine("IMPLEMENTATION 1: DIRECT CALLBACKS");
        Console.WriteLine("==========================================\n");
        TestCallbacksImplementation(requests);
        
        Console.WriteLine("\n==========================================");
        Console.WriteLine("IMPLEMENTATION 2: TASK-BASED (BONUS)");
        Console.WriteLine("==========================================\n");
        TestTasksImplementation(requests);
        
        Console.WriteLine("\n==========================================");
        Console.WriteLine("IMPLEMENTATION 3: ASYNC/AWAIT");
        Console.WriteLine("==========================================\n");
        TestAsyncImplementation(requests);
        
        Console.WriteLine("\n==========================================");
        Console.WriteLine("ALL TESTS COMPLETED");
        Console.WriteLine("==========================================");
    }

    /// <summary>
    /// Tests the callback-based implementation.
    /// Uses a CountdownEvent to wait for all downloads to complete.
    /// </summary>
    static void TestCallbacksImplementation(List<DownloadRequest> requests)
    {
        CountdownEvent countdown = new CountdownEvent(requests.Count);
        List<DownloadResult> results = new List<DownloadResult>();
        object resultsLock = new object();
        
        DateTime startTime = DateTime.Now;
        
        foreach (var request in requests)
        {
            HttpDownloaderCallbacks.Download(request, (DownloadResult result) =>
            {
                lock (resultsLock)
                {
                    results.Add(result);
                }
                countdown.Signal();
            });
        }
        
        // Wait for all downloads to complete (only allowed Wait() call as per requirements)
        countdown.Wait();
        
        TimeSpan elapsed = DateTime.Now - startTime;
        
        PrintResults("Direct Callbacks", results, elapsed);
    }

    /// <summary>
    /// Tests the task-based implementation with ContinueWith().
    /// Uses Task.WhenAll to wait for all downloads to complete.
    /// </summary>
    static void TestTasksImplementation(List<DownloadRequest> requests)
    {
        DateTime startTime = DateTime.Now;
        
        List<Task<DownloadResult>> tasks = new List<Task<DownloadResult>>();
        
        foreach (var request in requests)
        {
            tasks.Add(HttpDownloaderTasks.Download(request));
        }
        
        // Wait for all downloads to complete (only allowed Wait() call as per requirements)
        Task.WhenAll(tasks).Wait();
        
        TimeSpan elapsed = DateTime.Now - startTime;
        
        List<DownloadResult> results = tasks.Select(t => t.Result).ToList();
        
        PrintResults("Task-Based (ContinueWith)", results, elapsed);
    }

    /// <summary>
    /// Tests the async/await implementation.
    /// Uses Task.WhenAll to wait for all downloads to complete.
    /// </summary>
    static void TestAsyncImplementation(List<DownloadRequest> requests)
    {
        DateTime startTime = DateTime.Now;
        
        List<Task<DownloadResult>> tasks = new List<Task<DownloadResult>>();
        
        foreach (var request in requests)
        {
            tasks.Add(HttpDownloaderAsync.Download(request));
        }
        
        // Wait for all downloads to complete (only allowed Wait() call as per requirements)
        Task.WhenAll(tasks).Wait();
        
        TimeSpan elapsed = DateTime.Now - startTime;
        
        List<DownloadResult> results = tasks.Select(t => t.Result).ToList();
        
        PrintResults("Async/Await", results, elapsed);
    }

    /// <summary>
    /// Prints results summary for a test run.
    /// </summary>
    static void PrintResults(string implementationName, List<DownloadResult> results, TimeSpan elapsed)
    {
        Console.WriteLine($"\n--- {implementationName} Results ---");
        Console.WriteLine($"Total time: {elapsed.TotalMilliseconds:F2} ms");
        Console.WriteLine($"Downloads completed: {results.Count(r => r.Success)}/{results.Count}");
        Console.WriteLine();
        
        foreach (var result in results)
        {
            if (result.Success)
            {
                Console.WriteLine($"[{result.Id}] SUCCESS - Status: {result.StatusCode}, Size: {result.ContentLength} bytes, Content preview: {GetContentPreview(result.Content)}");
            }
            else
            {
                Console.WriteLine($"[{result.Id}] FAILED - Error: {result.Error}");
            }
        }
    }

    /// <summary>
    /// Gets a preview of the content (first 100 characters).
    /// </summary>
    static string GetContentPreview(string content)
    {
        if (string.IsNullOrEmpty(content))
        {
            return "(empty)";
        }
        
        string preview = content.Length > 100 ? content.Substring(0, 100) + "..." : content;
        return preview.Replace("\r", "").Replace("\n", " ");
    }
}
