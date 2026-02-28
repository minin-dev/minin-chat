      ******************************************************************
      * MININ-CHAT MESSAGE PROCESSOR v1.0
      * --------------------------------------------------------
      * THE ENTERPRISE-GRADE CHAT MESSAGE FORMATTER
      * POWERED BY COBOL - THE LANGUAGE THAT REFUSES TO DIE
      *
      * PROTOCOL: PIPE-DELIMITED INPUT FROM STDIN
      *   FORMAT|nick|message|room  -> Formatted message
      *   HELP                      -> Help text
      *   MOTD                      -> Message of the day
      *   SYSTEM|message            -> System announcement
      *   VALIDATE|/command         -> Validate command
      *
      * OUTPUT: OK|result  or  ERR|message
      ******************************************************************
       IDENTIFICATION DIVISION.
       PROGRAM-ID. MININ-CHAT.
       AUTHOR. MININ-DEV.

       DATA DIVISION.
       WORKING-STORAGE SECTION.
       01  WS-INPUT            PIC X(1024).
       01  WS-OUTPUT           PIC X(1024).
       01  WS-ACTION           PIC X(16).
       01  WS-PARAM1           PIC X(256).
       01  WS-PARAM2           PIC X(512).
       01  WS-PARAM3           PIC X(64).
       01  WS-IDX              PIC 9(4) VALUE 0.
       01  WS-START            PIC 9(4) VALUE 1.
       01  WS-LEN              PIC 9(4) VALUE 0.
       01  WS-PIPE-CNT         PIC 9(2) VALUE 0.
       01  WS-INPUT-LEN        PIC 9(4) VALUE 0.
       01  WS-HOURS            PIC X(2).
       01  WS-MINUTES          PIC X(2).
       01  WS-SECONDS          PIC X(2).
       01  WS-TIME-NOW         PIC X(8).
       01  WS-MSG-UPPER        PIC X(32).

       PROCEDURE DIVISION.
       MAIN-PARA.
           ACCEPT WS-INPUT
           MOVE FUNCTION LENGTH(FUNCTION TRIM(WS-INPUT
               TRAILING)) TO WS-INPUT-LEN
           PERFORM PARSE-PIPES
           PERFORM DISPATCH
           DISPLAY FUNCTION TRIM(WS-OUTPUT TRAILING)
           STOP RUN.

      ******************************************************************
      * PIPE-DELIMITED PARSER
      * Splits input on "|" into ACTION, PARAM1, PARAM2, PARAM3
      ******************************************************************
       PARSE-PIPES.
           MOVE SPACES TO WS-ACTION WS-PARAM1
               WS-PARAM2 WS-PARAM3
           MOVE 1 TO WS-START
           MOVE 0 TO WS-PIPE-CNT
           PERFORM VARYING WS-IDX FROM 1 BY 1
               UNTIL WS-IDX > WS-INPUT-LEN
               IF WS-INPUT(WS-IDX:1) = "|"
                   COMPUTE WS-LEN = WS-IDX - WS-START
                   ADD 1 TO WS-PIPE-CNT
                   PERFORM STORE-FIELD
                   COMPUTE WS-START = WS-IDX + 1
               END-IF
           END-PERFORM
           COMPUTE WS-LEN = WS-INPUT-LEN - WS-START + 1
           IF WS-LEN > 0
               ADD 1 TO WS-PIPE-CNT
               PERFORM STORE-FIELD
           END-IF.

       STORE-FIELD.
           IF WS-LEN > 0
               EVALUATE WS-PIPE-CNT
                   WHEN 1
                       MOVE WS-INPUT(WS-START:WS-LEN)
                           TO WS-ACTION
                   WHEN 2
                       MOVE WS-INPUT(WS-START:WS-LEN)
                           TO WS-PARAM1
                   WHEN 3
                       MOVE WS-INPUT(WS-START:WS-LEN)
                           TO WS-PARAM2
                   WHEN 4
                       MOVE WS-INPUT(WS-START:WS-LEN)
                           TO WS-PARAM3
               END-EVALUATE
           END-IF.

      ******************************************************************
      * COMMAND DISPATCHER
      ******************************************************************
       DISPATCH.
           MOVE SPACES TO WS-OUTPUT
           MOVE FUNCTION UPPER-CASE(
               FUNCTION TRIM(WS-ACTION)) TO WS-MSG-UPPER
           EVALUATE WS-MSG-UPPER
               WHEN "FORMAT"
                   PERFORM DO-FORMAT
               WHEN "HELP"
                   PERFORM DO-HELP
               WHEN "MOTD"
                   PERFORM DO-MOTD
               WHEN "SYSTEM"
                   PERFORM DO-SYSTEM
               WHEN "VALIDATE"
                   PERFORM DO-VALIDATE
               WHEN "STATUS"
                   PERFORM DO-STATUS
               WHEN OTHER
                   STRING "ERR|UNKNOWN:" DELIMITED BY SIZE
                       FUNCTION TRIM(WS-ACTION)
                           DELIMITED BY SIZE
                       INTO WS-OUTPUT
                   END-STRING
           END-EVALUATE.

      ******************************************************************
      * FORMAT MESSAGE: FORMAT|nick|message|room
      * Output: OK|[HH:MM:SS] <nick> message
      ******************************************************************
       DO-FORMAT.
           ACCEPT WS-TIME-NOW FROM TIME
           MOVE WS-TIME-NOW(1:2) TO WS-HOURS
           MOVE WS-TIME-NOW(3:2) TO WS-MINUTES
           MOVE WS-TIME-NOW(5:2) TO WS-SECONDS
           STRING "OK|[" DELIMITED BY SIZE
               WS-HOURS DELIMITED BY SIZE
               ":" DELIMITED BY SIZE
               WS-MINUTES DELIMITED BY SIZE
               ":" DELIMITED BY SIZE
               WS-SECONDS DELIMITED BY SIZE
               "] <" DELIMITED BY SIZE
               FUNCTION TRIM(WS-PARAM1)
                   DELIMITED BY SIZE
               "> " DELIMITED BY SIZE
               FUNCTION TRIM(WS-PARAM2)
                   DELIMITED BY SIZE
               INTO WS-OUTPUT
           END-STRING.

      ******************************************************************
      * HELP TEXT
      ******************************************************************
       DO-HELP.
           STRING "OK|"
               DELIMITED BY SIZE
               "=== MININ-CHAT COMMANDS === "
               DELIMITED BY SIZE
               "/nick <n> Set name | "
               DELIMITED BY SIZE
               "/join <r> Join room | "
               DELIMITED BY SIZE
               "/w <u> <m> Whisper | "
               DELIMITED BY SIZE
               "/users Online | "
               DELIMITED BY SIZE
               "/rooms Rooms | "
               DELIMITED BY SIZE
               "/status Info | "
               DELIMITED BY SIZE
               "/clear Clear | "
               DELIMITED BY SIZE
               "/help Help | "
               DELIMITED BY SIZE
               "/quit Quit"
               DELIMITED BY SIZE
               INTO WS-OUTPUT
           END-STRING.

      ******************************************************************
      * MESSAGE OF THE DAY
      ******************************************************************
       DO-MOTD.
           STRING "OK|"
               DELIMITED BY SIZE
               "========================================="
               DELIMITED BY SIZE
               " MININ-CHAT v1.0 "
               DELIMITED BY SIZE
               "========================================="
               DELIMITED BY SIZE
               " BACKEND: COBOL + FORTRAN + C "
               DELIMITED BY SIZE
               "| ENCRYPTION: FORTRAN STREAM CIPHER "
               DELIMITED BY SIZE
               "| FORMATTER: COBOL "
               DELIMITED BY SIZE
               "| TYPE /help FOR COMMANDS "
               DELIMITED BY SIZE
               "========================================="
               DELIMITED BY SIZE
               INTO WS-OUTPUT
           END-STRING.

      ******************************************************************
      * SYSTEM MESSAGE: SYSTEM|text
      ******************************************************************
       DO-SYSTEM.
           STRING "OK|*** " DELIMITED BY SIZE
               FUNCTION TRIM(WS-PARAM1)
                   DELIMITED BY SIZE
               " ***" DELIMITED BY SIZE
               INTO WS-OUTPUT
           END-STRING.

      ******************************************************************
      * VALIDATE COMMAND
      ******************************************************************
       DO-VALIDATE.
           EVALUATE TRUE
               WHEN WS-PARAM1(1:5) = "/nick"
                   MOVE "OK|NICK" TO WS-OUTPUT
               WHEN WS-PARAM1(1:5) = "/join"
                   MOVE "OK|JOIN" TO WS-OUTPUT
               WHEN WS-PARAM1(1:4) = "/msg"
                   MOVE "OK|MSG" TO WS-OUTPUT
               WHEN WS-PARAM1(1:2) = "/w"
                   MOVE "OK|WHISPER" TO WS-OUTPUT
               WHEN WS-PARAM1(1:6) = "/users"
                   MOVE "OK|USERS" TO WS-OUTPUT
               WHEN WS-PARAM1(1:6) = "/rooms"
                   MOVE "OK|ROOMS" TO WS-OUTPUT
               WHEN WS-PARAM1(1:5) = "/help"
                   MOVE "OK|HELP" TO WS-OUTPUT
               WHEN WS-PARAM1(1:5) = "/quit"
                   MOVE "OK|QUIT" TO WS-OUTPUT
               WHEN OTHER
                   STRING "ERR|UNKNOWN: "
                       DELIMITED BY SIZE
                       FUNCTION TRIM(WS-PARAM1)
                           DELIMITED BY SIZE
                       INTO WS-OUTPUT
                   END-STRING
           END-EVALUATE.

      ******************************************************************
      * SERVER STATUS
      ******************************************************************
       DO-STATUS.
           STRING "OK|"
               DELIMITED BY SIZE
               "[COBOL PROCESSOR ONLINE] "
               DELIMITED BY SIZE
               "Language: GnuCOBOL | "
               DELIMITED BY SIZE
               "Protocol: PIPE-DELIMITED | "
               DELIMITED BY SIZE
               "Actions: FORMAT,HELP,MOTD,"
               DELIMITED BY SIZE
               "SYSTEM,VALIDATE,STATUS"
               DELIMITED BY SIZE
               INTO WS-OUTPUT
           END-STRING.
