# Use an official Python runtime as a parent image
FROM python:3.11-slim

# Set the working directory in the container
WORKDIR /app

# Copy the requirements file into the container
COPY requirements.txt .

# Install any needed packages specified in requirements.txt
RUN pip install --no-cache-dir -r requirements.txt

# Copy the local certs directory to the image
# For production, use Docker secrets or secure volume mounts instead.
COPY ./certs /app/certs

# Copy the server script into the container
COPY server.py .

# Make port 8443 available to the world outside this container
EXPOSE 8443

# Define environment variables (optional, can be overridden)
ENV PYTHONUNBUFFERED 1

# Run server.py when the container launches
CMD ["python", "server.py"]