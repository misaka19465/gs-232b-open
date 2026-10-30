// NOTE: 仅使用本机 C 工具链链接并运行实际基线入口。
extern void app_main(void);

int main(void)
{
    app_main();
    return 0;
}
