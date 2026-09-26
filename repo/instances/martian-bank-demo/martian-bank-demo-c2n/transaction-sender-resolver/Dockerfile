# Copyright (c) 2023 Cisco Systems, Inc. and its affiliates All rights reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

FROM --platform=linux/amd64 python

RUN mkdir /service
COPY transaction-sender-resolver/ /service/transaction-sender-resolver/
WORKDIR /service/transaction-sender-resolver
RUN python -m pip install --upgrade pip
RUN python -m pip install -r requirements.txt

EXPOSE 50052

ENTRYPOINT ["python", "app.py"]
