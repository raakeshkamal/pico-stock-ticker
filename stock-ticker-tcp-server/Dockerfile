# Use an official Python runtime as a parent image
FROM python:3.10-slim

# Set the working directory in the container
WORKDIR /app

# Copy the requirements file into the container
COPY requirements.txt .

# Install any needed packages specified in requirements.txt
RUN pip install --no-cache-dir -r requirements.txt

# Copy the current directory contents into the container at /app
COPY . .

# Make port 8443 available to the world outside this container
EXPOSE 8443

# Define environment variable for the certificate directory (can be overridden)
ENV CERT_DIR=/app/certs

# Run server.py when the container launches
# The CLIENT_AUTH_TOKEN will be passed via `docker run -e`
CMD ["python", "server.py"]