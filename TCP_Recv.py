import socket

# 定义主机和端口
HOST = '0.0.0.0'  # 监听所有可用的网络接口
PORT = 9999       # 监听的端口号

# 创建一个TCP/IP套接字
server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

# 绑定套接字到地址和端口
server_socket.bind((HOST, PORT))

# 开始监听传入的连接
server_socket.listen(socket.SOMAXCONN)

print("等待客户端连接...")

while True:
    # 接受客户端连接
    client_socket, client_address = server_socket.accept()
    print("与客户端连接成功：", client_address)

    while True:
        # 接收数据
        data = client_socket.recv(1024)
        if not data:
            break
        print("收到数据：", data.decode('utf-8'))

# 关闭连接
client_socket.close()
server_socket.close()
print("与客户端断开连接")
