#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* 缓存节点结构 */
typedef struct cache_node {
    char *url;                      /* URL 作为键 */
    char *data;                     /* 缓存的响应数据 */
    size_t size;                    /* 数据大小 */
    struct cache_node *prev;        /* 前一个节点 */
    struct cache_node *next;        /* 后一个节点 */
} cache_node;

/* 缓存结构 */
typedef struct {
    cache_node *head;               /* 链表头（最近使用） */
    cache_node *tail;               /* 链表尾（最久未使用） */
    size_t total_size;              /* 当前缓存总大小 */
    pthread_rwlock_t lock;          /* 读写锁 */
} cache;

/* 全局缓存 */
cache web_cache;

/* 函数声明 */
void doit(int fd);
void *thread_routine(void *vargp);
void parse_uri(char *uri, char *hostname, char *path, char *port);
void build_request(rio_t *rio_client, char *request, char *hostname, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

/* 缓存函数 */
void cache_init();
cache_node *cache_find(char *url);
void cache_insert(char *url, char *data, size_t size);
void cache_evict();
void cache_move_to_front(cache_node *node);
void cache_remove_node(cache_node *node);

int main(int argc, char **argv)
{
    int listenfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    Signal(SIGPIPE, SIG_IGN);  /* 忽略 SIGPIPE 信号 */
    cache_init();  /* 初始化缓存 */
    listenfd = Open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        /* 创建线程处理请求 */
        Pthread_create(&tid, NULL, thread_routine, connfdp);
    }
    return 0;
}

/*
 * thread_routine - 线程例程，处理一个客户端连接
 */
void *thread_routine(void *vargp)
{
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());  /* 分离线程 */
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

/*
 * doit - 处理一个 HTTP 请求/响应事务
 */
void doit(int client_fd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    char request[MAXLINE];
    rio_t rio_client, rio_server;
    int server_fd;
    cache_node *cached;

    /* 读取客户端请求行 */
    Rio_readinitb(&rio_client, client_fd);
    if (!Rio_readlineb(&rio_client, buf, MAXLINE))
        return;

    printf("Request: %s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);

    /* 只支持 GET 方法 */
    if (strcasecmp(method, "GET")) {
        clienterror(client_fd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }

    /* 检查缓存 */
    cached = cache_find(uri);
    if (cached) {
        printf("Cache hit: %s\n", uri);
        Rio_writen(client_fd, cached->data, cached->size);
        return;
    }

    printf("Cache miss: %s\n", uri);

    /* 解析 URI 获取主机名、路径和端口 */
    parse_uri(uri, hostname, path, port);

    /* 构建发送给服务器的请求 */
    build_request(&rio_client, request, hostname, path);

    /* 连接到目标服务器 */
    server_fd = Open_clientfd(hostname, port);
    if (server_fd < 0) {
        clienterror(client_fd, hostname, "502", "Bad Gateway",
                    "Failed to connect to the server");
        return;
    }

    /* 发送请求到服务器 */
    Rio_readinitb(&rio_server, server_fd);
    Rio_writen(server_fd, request, strlen(request));

    /* 转发服务器响应给客户端，并缓存 */
    size_t n, total = 0;
    char cache_buf[MAX_OBJECT_SIZE];
    int cacheable = 1;

    while ((n = Rio_readnb(&rio_server, buf, MAXLINE)) > 0) {
        Rio_writen(client_fd, buf, n);

        /* 如果对象足够小，缓存它 */
        if (cacheable && total + n <= MAX_OBJECT_SIZE) {
            memcpy(cache_buf + total, buf, n);
            total += n;
        } else {
            cacheable = 0;
        }
    }

    /* 如果可以缓存，插入缓存 */
    if (cacheable && total > 0) {
        cache_insert(uri, cache_buf, total);
    }

    Close(server_fd);
}

/*
 * parse_uri - 解析 URI 为主机名、路径和端口
 * URI 格式: http://hostname:port/path 或 http://hostname/path
 */
void parse_uri(char *uri, char *hostname, char *path, char *port)
{
    char *ptr;
    char uri_copy[MAXLINE];

    strcpy(uri_copy, uri);

    /* 默认端口 */
    strcpy(port, "80");

    /* 跳过 "http://" */
    ptr = strstr(uri_copy, "//");
    if (ptr) {
        ptr += 2;
    } else {
        ptr = uri_copy;
    }

    /* 查找路径的开始 */
    char *path_start = strchr(ptr, '/');
    if (path_start) {
        strcpy(path, path_start);
        *path_start = '\0';  /* 截断以获取主机名部分 */
    } else {
        strcpy(path, "/");
    }

    /* 查找端口号 */
    char *port_start = strchr(ptr, ':');
    if (port_start) {
        *port_start = '\0';
        strcpy(port, port_start + 1);
    }

    strcpy(hostname, ptr);
}

/*
 * build_request - 构建发送给服务器的 HTTP 请求
 */
void build_request(rio_t *rio_client, char *request, char *hostname, char *path)
{
    char buf[MAXLINE];
    char request_hdr[MAXLINE];
    char host_hdr[MAXLINE];
    char other_hdr[MAXLINE];

    /* 构建请求行 */
    sprintf(request_hdr, "GET %s HTTP/1.0\r\n", path);

    /* 读取客户端的请求头 */
    int has_host = 0;
    strcpy(other_hdr, "");

    while (Rio_readlineb(rio_client, buf, MAXLINE) > 0) {
        if (strcmp(buf, "\r\n") == 0)
            break;

        /* 检查是否已有 Host 头 */
        if (strncasecmp(buf, "Host:", 5) == 0) {
            strcpy(host_hdr, buf);
            has_host = 1;
        }
        /* 跳过某些头部，使用我们自己的 */
        else if (strncasecmp(buf, "User-Agent:", 11) != 0 &&
                 strncasecmp(buf, "Connection:", 11) != 0 &&
                 strncasecmp(buf, "Proxy-Connection:", 17) != 0) {
            strcat(other_hdr, buf);
        }
    }

    /* 如果没有 Host 头，添加一个 */
    if (!has_host) {
        sprintf(host_hdr, "Host: %s\r\n", hostname);
    }

    /* 组装完整请求 */
    sprintf(request, "%s%s%s%s%s%s\r\n",
            request_hdr,
            host_hdr,
            user_agent_hdr,
            "Connection: close\r\n",
            "Proxy-Connection: close\r\n",
            other_hdr);
}

/*
 * clienterror - 返回错误消息给客户端
 */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE];

    /* 打印 HTTP 响应头 */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* 打印 HTTP 响应体 */
    sprintf(buf, "<html><title>Proxy Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=\"ffffff\">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Proxy server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}

/*
 * cache_init - 初始化缓存
 */
void cache_init()
{
    web_cache.head = NULL;
    web_cache.tail = NULL;
    web_cache.total_size = 0;
    pthread_rwlock_init(&web_cache.lock, NULL);
}

/*
 * cache_find - 在缓存中查找 URL
 */
cache_node *cache_find(char *url)
{
    pthread_rwlock_rdlock(&web_cache.lock);

    cache_node *node = web_cache.head;
    while (node) {
        if (strcmp(node->url, url) == 0) {
            pthread_rwlock_unlock(&web_cache.lock);

            /* 移到前面（需要写锁） */
            pthread_rwlock_wrlock(&web_cache.lock);
            cache_move_to_front(node);
            pthread_rwlock_unlock(&web_cache.lock);

            return node;
        }
        node = node->next;
    }

    pthread_rwlock_unlock(&web_cache.lock);
    return NULL;
}

/*
 * cache_insert - 插入新的缓存项
 */
void cache_insert(char *url, char *data, size_t size)
{
    if (size > MAX_OBJECT_SIZE)
        return;

    pthread_rwlock_wrlock(&web_cache.lock);

    /* 如果需要，驱逐旧项 */
    while (web_cache.total_size + size > MAX_CACHE_SIZE && web_cache.tail) {
        cache_evict();
    }

    /* 创建新节点 */
    cache_node *node = Malloc(sizeof(cache_node));
    node->url = Malloc(strlen(url) + 1);
    strcpy(node->url, url);
    node->data = Malloc(size);
    memcpy(node->data, data, size);
    node->size = size;
    node->prev = NULL;
    node->next = web_cache.head;

    if (web_cache.head) {
        web_cache.head->prev = node;
    }
    web_cache.head = node;

    if (!web_cache.tail) {
        web_cache.tail = node;
    }

    web_cache.total_size += size;

    pthread_rwlock_unlock(&web_cache.lock);
}

/*
 * cache_evict - 驱逐最久未使用的缓存项
 */
void cache_evict()
{
    if (!web_cache.tail)
        return;

    cache_node *node = web_cache.tail;
    cache_remove_node(node);

    web_cache.total_size -= node->size;

    Free(node->url);
    Free(node->data);
    Free(node);
}

/*
 * cache_move_to_front - 将节点移到链表头部
 */
void cache_move_to_front(cache_node *node)
{
    if (node == web_cache.head)
        return;

    cache_remove_node(node);

    node->prev = NULL;
    node->next = web_cache.head;

    if (web_cache.head) {
        web_cache.head->prev = node;
    }
    web_cache.head = node;

    if (!web_cache.tail) {
        web_cache.tail = node;
    }
}

/*
 * cache_remove_node - 从链表中移除节点
 */
void cache_remove_node(cache_node *node)
{
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        web_cache.head = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    } else {
        web_cache.tail = node->prev;
    }
}
