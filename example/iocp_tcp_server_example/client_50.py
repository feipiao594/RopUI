# WARNING: AIGC

import asyncio
import time

# 配置信息
SERVER_IP = '127.0.0.1'
SERVER_PORT = 8888
TOTAL_CLIENTS = 50
INTERVAL = 0.2  # 0.2秒间隔

async def start_client(client_id):
    """
    单个客户端的行为逻辑
    """
    try:
        # 1. 建立连接 (对应服务器的 AcceptEx)
        reader, writer = await asyncio.open_connection(SERVER_IP, SERVER_PORT)
        print(f"[Client {client_id:02d}] Connected.")

        # 2. 发送首包数据
        msg = f"Hello from client {client_id}\n"
        writer.write(msg.encode())
        await writer.drain()
        print(f"[Client {client_id:02d}] Sent: {msg.strip()}")

        # 3. 模拟业务处理，稍等一下再接收 (测试服务器是否能 hold 住连接)
        await asyncio.sleep(2) 

        # 4. 接收服务器回显
        data = await reader.read(1024)
        if data:
            print(f"[Client {client_id:02d}] Recv: {data.decode().strip()}")
        else:
            print(f"[Client {client_id:02d}] Server closed connection.")

        # 5. 关闭连接 (对应服务器的 stop/removeClient)
        writer.close()
        await writer.wait_closed()
        print(f"[Client {client_id:02d}] Socket Closed.")

    except Exception as e:
        print(f"[Client {client_id:02d}] Error: {e}")

async def main():
    print(f"--- Starting Stress Test: {TOTAL_CLIENTS} clients, {INTERVAL}s interval ---")
    start_time = time.time()
    
    tasks = []
    for i in range(1, TOTAL_CLIENTS + 1):
        # 创建一个异步任务并立即非阻塞执行
        task = asyncio.create_task(start_client(i))
        tasks.append(task)
        
        # 精准控制 0.2s 的发射间隔
        await asyncio.sleep(INTERVAL)
    
    # 等待所有客户端任务执行完毕
    await asyncio.gather(*tasks)
    
    end_time = time.time()
    print(f"--- Test Finished. Total time: {end_time - start_time:.2f}s ---")

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[User Interrupt] Stoping clients...")