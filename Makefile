NAME=voce

CC=gcc
CFLAGS=-std=c99 -Wall -Iinclude -o $(NAME) -DWITH_SSL
LDFLAGS=-pthread -lssl -lcrypto

ifeq ($(DEBUG),yes)
	CFLAGS+=-Wextra -Werror -pedantic
endif

voce: src/*.c include/*.h
	@echo -n Building $(NAME)...
	@$(CC) $(LDFLAGS) $(CFLAGS) src/*
	@echo " done."
	@echo ""
	@echo "   **********************************************************"
	@echo " **************************************************************"
	@echo " ** Congratulations! You have compiled Voce the IRC bot.     **"
	@echo " ** Voce is able to do lots of fun stuffs... Enjoy!          **"
	@echo " **                                                          **"
	@echo " ** To run voce use the command:                             **"
	@echo " ** ./voce                                                   **"
	@echo " **                                                          **"
	@echo " ** You may also specify the -d option to daemonize the bot. **"
	@echo " **************************************************************"
	@echo "   **********************************************************"
	@echo ""



clean:
	@echo -n Cleaning up build files...
	@rm -f $(NAME)
	@echo " done."
