FROM ubuntu:25.04
ARG arch_name=amd64
ARG code_server_version=4.106.2
ARG cpptools_version=1.29.0
ARG office_version=3.1.6

RUN cp /etc/apt/sources.list /etc/apt/sources.backup.list

# Please comment this line if you are not using Tencent Cloud network
# COPY sources.list /etc/apt/sources.list

RUN apt-get update
RUN apt-get install -y \
    cmake gdb vim emacs locales python3 python3-dev python3-pip python3-setuptools wget \
    curl sudo cgdb git \
    flex bison libgetopt-complete-perl valgrind

RUN useradd -m csapp && \
    echo "root ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers && \
    echo "csapp ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers 
RUN locale-gen en_US.UTF-8

ENV LANG=en_US.utf8 \
    LANGUAGE=en_US.UTF-8 \
    LC_ALL=en_US.UTF-8

# Network Packages
RUN wget https://github.com/coder/code-server/releases/download/v${code_server_version}/code-server_${code_server_version}_${arch_name}.deb && \
    dpkg -i code-server_${code_server_version}_${arch_name}.deb

RUN usermod -s /bin/bash csapp
RUN chsh -s /bin/bash csapp
ENV SHELL=/bin/bash 

# DataLab
RUN if [ "${arch_name}" = "arm64" ] ; then apt-get install -y g++-multilib-arm-linux-gnueabi; else apt-get install -y g++-multilib; fi

USER csapp

# Office Viewer Extension
RUN mkdir /home/csapp/extensions
ADD cweijan.vscode-office-${office_version}.vsix /home/csapp/extensions
RUN code-server --install-extension /home/csapp/extensions/cweijan.vscode-office-${office_version}.vsix

# Cpp Extension
RUN wget https://github.com/microsoft/vscode-cpptools/releases/download/v${cpptools_version}/cpptools-linux-x64.vsix -O /home/csapp/extensions/cpptools-linux-x64.vsix
RUN code-server --install-extension /home/csapp/extensions/cpptools-linux-x64.vsix

# Formatter Extension
RUN code-server --install-extension esbenp.prettier-vscode

# Markdown Extension
RUN code-server --install-extension yzhang.markdown-all-in-one

# Material Theme Extension
RUN code-server --install-extension vira.vsc-vira-theme 

USER root

RUN chown csapp -R /home/csapp/.local/share/

# RUN cp /etc/apt/sources.list /etc/apt/sources-tencent.list
# RUN mv /etc/apt/sources.backup.list /etc/apt/sources.list
# RUN apt-get update

EXPOSE 7777

ENV PASSWORD=csapp

USER csapp


CMD [ "code-server", "--bind-addr", "0.0.0.0:7777", "--auth", "password" ]
